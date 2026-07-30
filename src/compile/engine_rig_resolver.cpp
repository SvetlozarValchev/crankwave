#include "compile/engine_resolver_internal.hpp"

#include "compile/si_conversion.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

void add(authoring::DiagnosticReport &report, authoring::DiagnosticCode code,
         std::string path, std::string message) {
    authoring::Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.json_pointer = std::move(path);
    diagnostic.message = std::move(message);
    report.diagnostics.push_back(std::move(diagnostic));
}

void append(authoring::DiagnosticReport &destination,
            authoring::DiagnosticReport source) {
    for (auto &diagnostic : source.diagnostics) {
        destination.diagnostics.push_back(std::move(diagnostic));
    }
}

[[nodiscard]] std::optional<double>
admit_quantity(const authoring::Quantity &quantity,
               authoring::QuantityDimension dimension, std::string_view path,
               authoring::DiagnosticReport &report) {
    auto result = convert_quantity_to_si(quantity, dimension, path);
    if (auto *diagnostics = std::get_if<authoring::DiagnosticReport>(&result)) {
        append(report, std::move(*diagnostics));
        return std::nullopt;
    }
    return std::get<SiQuantity>(result).value;
}

[[nodiscard]] double resolved_quantity(const authoring::Quantity &quantity,
                                       authoring::QuantityDimension dimension,
                                       std::string_view path) {
    auto result = convert_quantity_to_si(quantity, dimension, path);
    return std::get<SiQuantity>(result).value;
}

[[nodiscard]] RuntimeObjectId runtime_id(const IdNamespace &ids,
                                         const std::string_view semantic_id) {
    return ids.by_semantic_id.at(std::string{semantic_id});
}

void require_canonical_id(authoring::DiagnosticReport &report, std::string_view value,
                          std::string path, std::string_view kind) {
    if (!contract::is_valid_semantic_id(value)) {
        add(report, authoring::DiagnosticCode::unsupported_capability, std::move(path),
            std::string{kind} + " ID must be a lowercase canonical semantic ID");
    }
}

void require_positive(authoring::DiagnosticReport &report,
                      const std::optional<double> value, std::string path,
                      std::string_view description) {
    if (value && !(*value > 0.0)) {
        add(report, authoring::DiagnosticCode::out_of_range, std::move(path),
            std::string{description} + " must be positive");
    }
}

void require_nonnegative(authoring::DiagnosticReport &report,
                         const std::optional<double> value, std::string path,
                         std::string_view description) {
    if (value && *value < 0.0) {
        add(report, authoring::DiagnosticCode::out_of_range, std::move(path),
            std::string{description} + " must be nonnegative");
    }
}

} // namespace

void admit_engine_rig(ModelContext &context, authoring::DiagnosticReport &report) {
    if (!context.document.rig) {
        return;
    }

    const auto &rig = *context.document.rig;
    require_canonical_id(report, rig.id.value, "/rig/id", "rig");
    if (!rig.vehicle && !rig.transmission && !rig.dyno_defaults) {
        add(report, authoring::DiagnosticCode::missing_value, "/rig",
            "rig must define a vehicle, transmission, or dyno defaults");
    }

    if (rig.vehicle) {
        const auto &vehicle = *rig.vehicle;
        require_canonical_id(report, vehicle.id.value, "/rig/vehicle/id", "vehicle");
        require_positive(report,
                         admit_quantity(vehicle.mass,
                                        authoring::QuantityDimension::mass,
                                        "/rig/vehicle/mass", report),
                         "/rig/vehicle/mass", "vehicle mass");
        if (!std::isfinite(vehicle.drag_coefficient) ||
            vehicle.drag_coefficient < 0.0) {
            add(report, authoring::DiagnosticCode::out_of_range,
                "/rig/vehicle/drag_coefficient",
                "vehicle drag coefficient must be finite and nonnegative");
        }
        require_positive(report,
                         admit_quantity(vehicle.frontal_area,
                                        authoring::QuantityDimension::area,
                                        "/rig/vehicle/frontal_area", report),
                         "/rig/vehicle/frontal_area", "vehicle frontal area");
        if (!std::isfinite(vehicle.differential_ratio) ||
            !(vehicle.differential_ratio > 0.0)) {
            add(report, authoring::DiagnosticCode::out_of_range,
                "/rig/vehicle/differential_ratio",
                "vehicle differential ratio must be finite and positive");
        }
        require_positive(report,
                         admit_quantity(vehicle.tire_radius,
                                        authoring::QuantityDimension::length,
                                        "/rig/vehicle/tire_radius", report),
                         "/rig/vehicle/tire_radius", "vehicle tire radius");
        require_nonnegative(report,
                            admit_quantity(vehicle.rolling_resistance_force,
                                           authoring::QuantityDimension::force,
                                           "/rig/vehicle/rolling_resistance_force",
                                           report),
                            "/rig/vehicle/rolling_resistance_force",
                            "vehicle rolling-resistance force");
    }

    if (rig.transmission) {
        const auto &transmission = *rig.transmission;
        require_canonical_id(report, transmission.id.value, "/rig/transmission/id",
                             "transmission");
        require_positive(
            report,
            admit_quantity(transmission.maximum_clutch_torque,
                           authoring::QuantityDimension::torque,
                           "/rig/transmission/maximum_clutch_torque", report),
            "/rig/transmission/maximum_clutch_torque", "maximum clutch torque");
        if (transmission.gears.empty()) {
            add(report, authoring::DiagnosticCode::missing_value,
                "/rig/transmission/gears",
                "transmission must define at least one gear");
        }
        std::unordered_set<std::string> gear_ids;
        for (std::size_t index = 0; index < transmission.gears.size(); ++index) {
            const auto &gear = transmission.gears[index];
            const auto base = "/rig/transmission/gears/" + std::to_string(index);
            require_canonical_id(report, gear.id.value, base + "/id", "gear");
            if (!gear_ids.insert(gear.id.value).second) {
                add(report, authoring::DiagnosticCode::duplicate_id, base + "/id",
                    "gear IDs must be unique");
            }
            if (!std::isfinite(gear.ratio) || gear.ratio == 0.0) {
                add(report, authoring::DiagnosticCode::out_of_range, base + "/ratio",
                    "gear ratio must be finite and nonzero");
            }
        }
    }

    if (rig.dyno_defaults) {
        const auto &dyno = *rig.dyno_defaults;
        const auto minimum = admit_quantity(
            dyno.minimum_engine_speed, authoring::QuantityDimension::angular_speed,
            "/rig/dyno_defaults/minimum_engine_speed", report);
        const auto maximum = admit_quantity(
            dyno.maximum_engine_speed, authoring::QuantityDimension::angular_speed,
            "/rig/dyno_defaults/maximum_engine_speed", report);
        const auto hold_step =
            admit_quantity(dyno.hold_step, authoring::QuantityDimension::angular_speed,
                           "/rig/dyno_defaults/hold_step", report);
        require_nonnegative(report, minimum, "/rig/dyno_defaults/minimum_engine_speed",
                            "minimum dyno engine speed");
        require_positive(report, maximum, "/rig/dyno_defaults/maximum_engine_speed",
                         "maximum dyno engine speed");
        require_positive(report, hold_step, "/rig/dyno_defaults/hold_step",
                         "dyno hold step");
        if (minimum && maximum && !(*minimum < *maximum)) {
            add(report, authoring::DiagnosticCode::inconsistent_value,
                "/rig/dyno_defaults/maximum_engine_speed",
                "maximum dyno engine speed must exceed minimum engine speed "
                "after SI conversion");
        }
    }
}

std::optional<ResolvedRigDescriptor> assemble_rig(const ModelContext &context,
                                                  ResolutionEmitter &emitter) {
    if (!context.document.rig) {
        return std::nullopt;
    }

    const auto &source = *context.document.rig;
    ResolvedRigDescriptor rig;
    rig.runtime_id = runtime_id(context.ids.rigs, source.id.value);
    rig.semantic_id = emitter.authored(source.id.value, "rig.semantic_id");

    if (source.vehicle) {
        const auto &vehicle = *source.vehicle;
        ResolvedVehicleDescriptor output;
        output.runtime_id = runtime_id(context.ids.vehicles, vehicle.id.value);
        output.semantic_id =
            emitter.authored(vehicle.id.value, "rig.vehicle.semantic_id");
        output.mass_kg = emitter.authored(
            resolved_quantity(vehicle.mass, authoring::QuantityDimension::mass,
                              "/rig/vehicle/mass"),
            "rig.vehicle.mass_kg");
        output.drag_coefficient =
            emitter.authored(vehicle.drag_coefficient, "rig.vehicle.drag_coefficient");
        output.frontal_area_m2 = emitter.authored(
            resolved_quantity(vehicle.frontal_area, authoring::QuantityDimension::area,
                              "/rig/vehicle/frontal_area"),
            "rig.vehicle.frontal_area_m2");
        output.differential_ratio = emitter.authored(vehicle.differential_ratio,
                                                     "rig.vehicle.differential_ratio");
        output.tire_radius_m = emitter.authored(
            resolved_quantity(vehicle.tire_radius, authoring::QuantityDimension::length,
                              "/rig/vehicle/tire_radius"),
            "rig.vehicle.tire_radius_m");
        output.rolling_resistance_force_n =
            emitter.authored(resolved_quantity(vehicle.rolling_resistance_force,
                                               authoring::QuantityDimension::force,
                                               "/rig/vehicle/rolling_resistance_force"),
                             "rig.vehicle.rolling_resistance_force_n");
        rig.vehicle = std::move(output);
    }

    if (source.transmission) {
        const auto &transmission = *source.transmission;
        ResolvedTransmissionDescriptor output;
        output.runtime_id =
            runtime_id(context.ids.transmissions, transmission.id.value);
        output.semantic_id =
            emitter.authored(transmission.id.value, "rig.transmission.semantic_id");
        output.maximum_clutch_torque_nm = emitter.authored(
            resolved_quantity(transmission.maximum_clutch_torque,
                              authoring::QuantityDimension::torque,
                              "/rig/transmission/maximum_clutch_torque"),
            "rig.transmission.maximum_clutch_torque_nm");
        output.gears.reserve(transmission.gears.size());
        for (std::size_t index = 0; index < transmission.gears.size(); ++index) {
            const auto &gear = transmission.gears[index];
            const auto base = "rig.transmission.gears." + gear.id.value;
            output.gears.push_back({
                runtime_id(context.ids.gears, gear.id.value),
                emitter.authored(static_cast<std::uint32_t>(index + 1U),
                                 base + ".authored_ordinal"),
                emitter.authored(gear.id.value, base + ".semantic_id"),
                emitter.authored(gear.ratio, base + ".ratio"),
            });
        }
        rig.transmission = std::move(output);
    }

    if (source.dyno_defaults) {
        const auto &dyno = *source.dyno_defaults;
        rig.dyno_defaults = ResolvedDynoDefaultsDescriptor{
            emitter.authored(
                resolved_quantity(dyno.minimum_engine_speed,
                                  authoring::QuantityDimension::angular_speed,
                                  "/rig/dyno_defaults/minimum_engine_speed"),
                "rig.dyno_defaults.minimum_engine_speed_rad_s"),
            emitter.authored(
                resolved_quantity(dyno.maximum_engine_speed,
                                  authoring::QuantityDimension::angular_speed,
                                  "/rig/dyno_defaults/maximum_engine_speed"),
                "rig.dyno_defaults.maximum_engine_speed_rad_s"),
            emitter.authored(
                resolved_quantity(dyno.hold_step,
                                  authoring::QuantityDimension::angular_speed,
                                  "/rig/dyno_defaults/hold_step"),
                "rig.dyno_defaults.hold_step_rad_s"),
        };
    }

    return rig;
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
