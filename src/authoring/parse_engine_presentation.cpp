#include "authoring/parse_engine_detail.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <utility>

namespace engine_sim_offline::authoring::detail {
namespace {

void parse_audio_asset(DocumentReader &reader, JsonValue value, std::string_view path,
                       AudioAssetDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "kind", "uri", "sha256"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("audio_asset", output.id.value);
    read_enum(reader, reader.required(value, "kind", path, owner),
              pointer_member(path, "kind"),
              {{"impulse_response", AudioAssetKind::impulse_response},
               {"audio_sample", AudioAssetKind::audio_sample}},
              output.kind, owner);
    reader.string(reader.required(value, "uri", path, owner),
                  pointer_member(path, "uri"), output.uri, owner);
    if (output.uri.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "uri"),
                   "asset URI must not be empty", owner);
    }
    const auto digest = reader.optional(value, "sha256");
    if (digest.valid() && !digest.is_null()) {
        std::string parsed;
        if (reader.string(digest, pointer_member(path, "sha256"), parsed, owner)) {
            const auto is_hex =
                parsed.size() == 64U && std::ranges::all_of(parsed, [](char byte) {
                    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
                });
            if (!is_hex) {
                reader.add(DiagnosticCode::invalid_value,
                           pointer_member(path, "sha256"),
                           "SHA-256 must be 64 lowercase hexadecimal digits", owner);
            } else {
                output.sha256 = std::move(parsed);
            }
        }
    }
}

void parse_cylinder_route(DocumentReader &reader, JsonValue value,
                          std::string_view path, CylinderRoutePresentation &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"cylinder", "route", "gain_linear"});
    read_ref_member(reader, value, "cylinder", path, output.cylinder);
    const auto owner = subject("cylinder", output.cylinder.value);
    read_ref_member(reader, value, "route", path, output.route, owner);
    reader.nonnegative_number(reader.required(value, "gain_linear", path, owner),
                              pointer_member(path, "gain_linear"), output.gain_linear,
                              owner);
}

void parse_route(DocumentReader &reader, JsonValue value, std::string_view path,
                 RoutePresentation &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"route", "source_gain_linear", "impulse_response",
                           "impulse_response_gain_linear", "wet_mix_01"});
    read_ref_member(reader, value, "route", path, output.route);
    const auto owner = subject("source_route", output.route.value);
    reader.nonnegative_number(reader.required(value, "source_gain_linear", path, owner),
                              pointer_member(path, "source_gain_linear"),
                              output.source_gain_linear, owner);
    const auto impulse = reader.optional(value, "impulse_response");
    if (impulse.valid() && !impulse.is_null()) {
        AudioAssetRef parsed;
        if (reader.ref(impulse, pointer_member(path, "impulse_response"), parsed,
                       owner)) {
            output.impulse_response = std::move(parsed);
        }
    }
    reader.nonnegative_number(
        reader.required(value, "impulse_response_gain_linear", path, owner),
        pointer_member(path, "impulse_response_gain_linear"),
        output.impulse_response_gain_linear, owner);
    reader.fraction(reader.required(value, "wet_mix_01", path, owner),
                    pointer_member(path, "wet_mix_01"), output.wet_mix_01, owner);
}

void parse_conditioning(DocumentReader &reader, JsonValue value, std::string_view path,
                        PresentationConditioning &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"jitter_scale", "jitter_modulation_cutoff_frequency",
                           "derivative_mix_01", "air_noise_mix_01",
                           "air_noise_cutoff_frequency"});
    reader.nonnegative_number(reader.required(value, "jitter_scale", path),
                              pointer_member(path, "jitter_scale"),
                              output.jitter_scale);
    read_quantity_member(reader, value, "jitter_modulation_cutoff_frequency", path,
                         QuantityDimension::frequency,
                         output.jitter_modulation_cutoff_frequency);
    reader.fraction(reader.required(value, "derivative_mix_01", path),
                    pointer_member(path, "derivative_mix_01"),
                    output.derivative_mix_01);
    reader.fraction(reader.required(value, "air_noise_mix_01", path),
                    pointer_member(path, "air_noise_mix_01"), output.air_noise_mix_01);
    read_quantity_member(reader, value, "air_noise_cutoff_frequency", path,
                         QuantityDimension::frequency,
                         output.air_noise_cutoff_frequency);
    require_positive(reader, output.jitter_modulation_cutoff_frequency,
                     pointer_member(path, "jitter_modulation_cutoff_frequency"));
    require_positive(reader, output.air_noise_cutoff_frequency,
                     pointer_member(path, "air_noise_cutoff_frequency"));
}

void parse_audio_bus(DocumentReader &reader, JsonValue value, std::string_view path,
                     AudioBusDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "routes", "gain_linear", "publish"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("audio_bus", output.id.value);
    read_required_array(
        reader, value, "routes", path, output.routes,
        [&](JsonValue item, std::string_view item_path, SourceRouteRef &reference) {
            reader.ref(item, item_path, reference, owner);
        },
        owner);
    reader.nonnegative_number(reader.required(value, "gain_linear", path, owner),
                              pointer_member(path, "gain_linear"), output.gain_linear,
                              owner);
    reader.boolean(reader.required(value, "publish", path, owner),
                   pointer_member(path, "publish"), output.publish, owner);
}

void parse_audition(DocumentReader &reader, JsonValue value, std::string_view path,
                    AuditionMixDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"buses", "volume_linear", "fade_in", "fade_out"});
    read_required_array(
        reader, value, "buses", path, output.buses,
        [&](JsonValue item, std::string_view item_path, AudioBusRef &reference) {
            reader.ref(item, item_path, reference);
        });
    reader.nonnegative_number(reader.required(value, "volume_linear", path),
                              pointer_member(path, "volume_linear"),
                              output.volume_linear);
    read_quantity_member(reader, value, "fade_in", path, QuantityDimension::duration,
                         output.fade_in);
    read_quantity_member(reader, value, "fade_out", path, QuantityDimension::duration,
                         output.fade_out);
    require_nonnegative(reader, output.fade_in, pointer_member(path, "fade_in"));
    require_nonnegative(reader, output.fade_out, pointer_member(path, "fade_out"));
}

void parse_vehicle(DocumentReader &reader, JsonValue value, std::string_view path,
                   VehicleDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "mass", "drag_coefficient", "frontal_area",
                           "differential_ratio", "tire_radius",
                           "rolling_resistance_force", "maximum_service_brake_force"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("vehicle", output.id.value);
    read_quantity_member(reader, value, "mass", path, QuantityDimension::mass,
                         output.mass, owner);
    reader.nonnegative_number(reader.required(value, "drag_coefficient", path, owner),
                              pointer_member(path, "drag_coefficient"),
                              output.drag_coefficient, owner);
    read_quantity_member(reader, value, "frontal_area", path, QuantityDimension::area,
                         output.frontal_area, owner);
    reader.number(reader.required(value, "differential_ratio", path, owner),
                  pointer_member(path, "differential_ratio"), output.differential_ratio,
                  owner);
    if (!(output.differential_ratio > 0.0)) {
        reader.add(DiagnosticCode::out_of_range,
                   pointer_member(path, "differential_ratio"),
                   "differential ratio must be positive", owner);
    }
    read_quantity_member(reader, value, "tire_radius", path, QuantityDimension::length,
                         output.tire_radius, owner);
    read_quantity_member(reader, value, "rolling_resistance_force", path,
                         QuantityDimension::force, output.rolling_resistance_force,
                         owner);
    const auto maximum_service_brake_force =
        reader.optional(value, "maximum_service_brake_force");
    if (maximum_service_brake_force.valid() && !maximum_service_brake_force.is_null()) {
        Quantity parsed;
        reader.quantity(maximum_service_brake_force,
                        pointer_member(path, "maximum_service_brake_force"),
                        QuantityDimension::force, parsed, owner);
        require_positive(reader, parsed,
                         pointer_member(path, "maximum_service_brake_force"), owner);
        output.maximum_service_brake_force = std::move(parsed);
    }
    require_positive(reader, output.mass, pointer_member(path, "mass"), owner);
    require_positive(reader, output.frontal_area, pointer_member(path, "frontal_area"),
                     owner);
    require_positive(reader, output.tire_radius, pointer_member(path, "tire_radius"),
                     owner);
    require_nonnegative(reader, output.rolling_resistance_force,
                        pointer_member(path, "rolling_resistance_force"), owner);
}

void parse_gear(DocumentReader &reader, JsonValue value, std::string_view path,
                GearDefinition &output,
                const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "ratio"}, subject_value);
    read_id_member(reader, value, "id", path, output.id, subject_value);
    const auto owner = subject("gear", output.id.value);
    reader.number(reader.required(value, "ratio", path, owner),
                  pointer_member(path, "ratio"), output.ratio, owner);
    if (!(output.ratio > 0.0)) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "ratio"),
                   "forward gear ratio must be positive", owner);
    }
}

void parse_transmission(DocumentReader &reader, JsonValue value, std::string_view path,
                        TransmissionDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "maximum_clutch_torque", "gears"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("transmission", output.id.value);
    read_quantity_member(reader, value, "maximum_clutch_torque", path,
                         QuantityDimension::torque, output.maximum_clutch_torque,
                         owner);
    read_required_array(
        reader, value, "gears", path, output.gears,
        [&](JsonValue item, std::string_view item_path, GearDefinition &gear) {
            parse_gear(reader, item, item_path, gear, owner);
        },
        owner);
    require_positive(reader, output.maximum_clutch_torque,
                     pointer_member(path, "maximum_clutch_torque"), owner);
}

void parse_dyno_defaults(DocumentReader &reader, JsonValue value, std::string_view path,
                         DynoDefaultsDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path, {"minimum_engine_speed", "maximum_engine_speed", "hold_step"});
    read_quantity_member(reader, value, "minimum_engine_speed", path,
                         QuantityDimension::angular_speed, output.minimum_engine_speed);
    read_quantity_member(reader, value, "maximum_engine_speed", path,
                         QuantityDimension::angular_speed, output.maximum_engine_speed);
    read_quantity_member(reader, value, "hold_step", path,
                         QuantityDimension::angular_speed, output.hold_step);
    require_nonnegative(reader, output.minimum_engine_speed,
                        pointer_member(path, "minimum_engine_speed"));
    require_positive(reader, output.maximum_engine_speed,
                     pointer_member(path, "maximum_engine_speed"));
    require_positive(reader, output.hold_step, pointer_member(path, "hold_step"));
    if (output.minimum_engine_speed.unit == output.maximum_engine_speed.unit &&
        output.minimum_engine_speed.value >= output.maximum_engine_speed.value) {
        reader.add(DiagnosticCode::inconsistent_value,
                   pointer_member(path, "maximum_engine_speed"),
                   "maximum engine speed must exceed minimum engine speed");
    }
}

} // namespace

void parse_presentation(DocumentReader &reader, JsonValue value, std::string_view path,
                        PresentationDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"assets", "cylinder_routes", "routes", "conditioning",
                           "buses", "audition", "publication_gain_linear"});
    read_required_array(
        reader, value, "assets", path, output.assets,
        [&](JsonValue item, std::string_view item_path, AudioAssetDefinition &asset) {
            parse_audio_asset(reader, item, item_path, asset);
        });
    read_required_array(reader, value, "cylinder_routes", path, output.cylinder_routes,
                        [&](JsonValue item, std::string_view item_path,
                            CylinderRoutePresentation &route) {
                            parse_cylinder_route(reader, item, item_path, route);
                        });
    read_required_array(
        reader, value, "routes", path, output.routes,
        [&](JsonValue item, std::string_view item_path, RoutePresentation &route) {
            parse_route(reader, item, item_path, route);
        });
    parse_conditioning(reader, reader.required(value, "conditioning", path),
                       pointer_member(path, "conditioning"), output.conditioning);
    read_required_array(
        reader, value, "buses", path, output.buses,
        [&](JsonValue item, std::string_view item_path, AudioBusDefinition &bus) {
            parse_audio_bus(reader, item, item_path, bus);
        });
    parse_audition(reader, reader.required(value, "audition", path),
                   pointer_member(path, "audition"), output.audition);
    reader.nonnegative_number(reader.required(value, "publication_gain_linear", path),
                              pointer_member(path, "publication_gain_linear"),
                              output.publication_gain_linear);
}

void parse_rig(DocumentReader &reader, JsonValue value, std::string_view path,
               RigDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "vehicle", "transmission", "dyno_defaults"});
    read_id_member(reader, value, "id", path, output.id);
    const auto vehicle = reader.optional(value, "vehicle");
    if (vehicle.valid() && !vehicle.is_null()) {
        VehicleDefinition parsed;
        parse_vehicle(reader, vehicle, pointer_member(path, "vehicle"), parsed);
        output.vehicle = std::move(parsed);
    }
    const auto transmission = reader.optional(value, "transmission");
    if (transmission.valid() && !transmission.is_null()) {
        TransmissionDefinition parsed;
        parse_transmission(reader, transmission, pointer_member(path, "transmission"),
                           parsed);
        output.transmission = std::move(parsed);
    }
    const auto dyno_defaults = reader.optional(value, "dyno_defaults");
    if (dyno_defaults.valid() && !dyno_defaults.is_null()) {
        DynoDefaultsDefinition parsed;
        parse_dyno_defaults(reader, dyno_defaults,
                            pointer_member(path, "dyno_defaults"), parsed);
        output.dyno_defaults = std::move(parsed);
    }
}

} // namespace engine_sim_offline::authoring::detail
