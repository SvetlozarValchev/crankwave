#include "compile/scenario_resolver_internal.hpp"

#include "simulation/legacy_gas_primitives.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace crankwave::compile::detail::scenario_resolution {
namespace {

// The admitted simulation -> capture -> excitation method consumes one exact 20 ms
// capture block and projects it to one exact 3,840-frame delivery block. The capture
// frame count therefore follows the authored 10 or 20 kHz capture clock.
constexpr std::uint64_t kMethodBlocksPerSecond = 50U;
constexpr std::uint32_t kDeliveryFramesPerMethodBlock = 3840U;

[[nodiscard]] std::optional<std::uint32_t>
capture_frames_per_method_block(const contract::RationalRateHz &capture_rate) noexcept {
    if (capture_rate != contract::RationalRateHz{10000U, 1U} &&
        capture_rate != contract::RationalRateHz{20000U, 1U}) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(capture_rate.numerator / kMethodBlocksPerSecond);
}

[[nodiscard]] std::optional<std::uint32_t>
internal_event_journal_capacity(const std::size_t cylinder_count,
                                const std::uint32_t capture_frames) noexcept {
    constexpr auto kMaximum =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (cylinder_count > (kMaximum - 1U) / 3U) {
        return std::nullopt;
    }
    const auto events_per_physics_frame = 3U * cylinder_count + 1U;
    if (events_per_physics_frame > kMaximum / capture_frames) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(events_per_physics_frame * capture_frames);
}

} // namespace

void ScenarioResolver::compile_common_fields() {
    scenario_.schema_version = 1U;
    scenario_.scenario_id = document_.id.value;
    scenario_.engine_profile_id = context_.engine.profile_id.value;

    scenario_.ambient.pressure_pa_abs.value =
        quantity(document_.ambient.pressure, authoring::QuantityDimension::pressure,
                 "/ambient/pressure");
    scenario_.ambient.temperature_k.value =
        quantity(document_.ambient.temperature,
                 authoring::QuantityDimension::temperature, "/ambient/temperature");
    scenario_.ambient.relative_humidity_01.value =
        document_.ambient.relative_humidity_01;

    const auto &fuel = *selected_fuel_.descriptor;
    scenario_.fuel.fuel_id.value = fuel.fuel_id.value;
    scenario_.fuel.lower_heating_value_j_per_kg.value =
        fuel.lower_heating_value_j_per_kg.value;
    scenario_.fuel.stoichiometric_air_fuel_mass_ratio.value =
        simulation::legacy_pseudo_gas_stoichiometric_mass_afr(
            fuel.molecular_air_fuel_ratio.value, fuel.molecular_mass_kg_per_mol.value);

    scenario_.initial_thermal_state.gas_temperature_k.value =
        quantity(document_.initial_thermal_state.gas_temperature,
                 authoring::QuantityDimension::temperature,
                 "/initial_thermal_state/gas_temperature");
    scenario_.initial_thermal_state.wall_temperature_k.value =
        quantity(document_.initial_thermal_state.wall_temperature,
                 authoring::QuantityDimension::temperature,
                 "/initial_thermal_state/wall_temperature");
    scenario_.initial_thermal_state.coolant_temperature_k.value =
        quantity(document_.initial_thermal_state.coolant_temperature,
                 authoring::QuantityDimension::temperature,
                 "/initial_thermal_state/coolant_temperature");
    scenario_.initial_thermal_state.oil_temperature_k.value =
        quantity(document_.initial_thermal_state.oil_temperature,
                 authoring::QuantityDimension::temperature,
                 "/initial_thermal_state/oil_temperature");

    scenario_.crankcase.pressure_pa_abs.value =
        quantity(document_.crankcase.pressure, authoring::QuantityDimension::pressure,
                 "/crankcase/pressure");
    scenario_.crankcase.temperature_k.value =
        quantity(document_.crankcase.temperature,
                 authoring::QuantityDimension::temperature, "/crankcase/temperature");

    scenario_.rates = {
        rate(document_.rates.physics, "/rates/physics"),
        rate(document_.rates.capture, "/rates/capture"),
        rate(document_.rates.source_processing, "/rates/source_processing"),
        rate(document_.rates.acoustics, "/rates/acoustics"),
        rate(document_.rates.delivery, "/rates/delivery"),
    };
    scenario_.total_duration_s.value =
        quantity(document_.total_duration, authoring::QuantityDimension::duration,
                 "/total_duration");
    scenario_.audible_start_s.value =
        quantity(document_.audible_start, authoring::QuantityDimension::duration,
                 "/audible_start");
    scenario_.audible_duration_s.value =
        quantity(document_.audible_duration, authoring::QuantityDimension::duration,
                 "/audible_duration");
    request_input_.session_capacities = {
        document_.quality.process_block_capacity_frames,
        document_.quality.event_queue_capacity,
        document_.quality.telemetry_capacity_frames,
    };
    if (document_.quality.process_block_capacity_frames <
        kDeliveryFramesPerMethodBlock) {
        add(authoring::DiagnosticCode::unsupported_capability,
            "/quality/process_block_capacity_frames",
            "the current executable method requires capacity for one exact "
            "3,840-frame delivery block");
    }
    if (document_.quality.event_queue_capacity == 0U) {
        add(authoring::DiagnosticCode::out_of_range, "/quality/event_queue_capacity",
            "caller control-command queue capacity must be positive");
    }
    if (document_.quality.telemetry_capacity_frames == 0U) {
        add(authoring::DiagnosticCode::out_of_range,
            "/quality/telemetry_capacity_frames",
            "returned telemetry-frame capacity must be positive");
    }

    const auto capture_frames =
        capture_frames_per_method_block(scenario_.rates.capture);
    if (!capture_frames.has_value()) {
        add(authoring::DiagnosticCode::unsupported_capability, "/rates/capture",
            "capture rate must use an exact 10000/1 or 20000/1 executable clock "
            "with a complete 20 ms method quantum");
    }
    const auto derived_event_capacity =
        capture_frames.has_value()
            ? internal_event_journal_capacity(context_.engine.cylinders.size(),
                                              *capture_frames)
            : std::nullopt;
    if (capture_frames.has_value() && !derived_event_capacity.has_value()) {
        add(authoring::DiagnosticCode::unsupported_capability, "/engine",
            "engine cylinder count cannot be represented by the current bounded "
            "internal event journal");
    }
    scenario_.quality.value = {
        document_.quality.id,
        1U,
        capture_frames.value_or(0U),
        derived_event_capacity.value_or(0U),
    };
    scenario_.public_seed.value = document_.public_seed;

    const auto total_physics = contract::resolve_frame_index(
        scenario_.total_duration_s.value, scenario_.rates.physics);
    if (!total_physics.has_value() || *total_physics == 0U) {
        add(authoring::DiagnosticCode::inconsistent_value, "/total_duration",
            "total duration must be a positive integer physics-frame horizon");
    } else {
        request_input_.total_physics_frames = *total_physics;
    }
    const auto audible_delivery = contract::resolve_frame_index(
        scenario_.audible_duration_s.value, scenario_.rates.delivery);
    if (!audible_delivery.has_value() || *audible_delivery == 0U) {
        add(authoring::DiagnosticCode::inconsistent_value, "/audible_duration",
            "audible duration must be a positive integer delivery-frame interval");
    } else {
        request_input_.audible_delivery_frames = *audible_delivery;
    }

    request_input_.authored_initial_engine_speed_rpm = engine_speed_rpm(
        document_.initial_state.engine_speed, "/initial_state/engine_speed");
    initial_theta_rad_ =
        quantity(document_.initial_state.crank_angle,
                 authoring::QuantityDimension::angle, "/initial_state/crank_angle");
}

void ScenarioResolver::compile_preparation() {
    std::visit(
        [&](const auto &preparation) {
            using T = std::decay_t<decltype(preparation)>;
            if constexpr (std::is_same_v<T, authoring::FixedSettlingPreparation>) {
                contract::FixedSettling resolved;
                resolved.warm_up_duration_s.value =
                    quantity(preparation.warm_up_duration,
                             authoring::QuantityDimension::duration,
                             "/preparation/warm_up_duration");
                resolved.settling_duration_s.value =
                    quantity(preparation.settling_duration,
                             authoring::QuantityDimension::duration,
                             "/preparation/settling_duration");
                scenario_.preparation = std::move(resolved);
            } else {
                contract::FixedHorizonCycleSampling resolved;
                resolved.method.value =
                    contract::fixed_horizon_cycle_sampling_method_identity();
                resolved.fixed_preparation_horizon_s.value =
                    quantity(preparation.preparation_duration,
                             authoring::QuantityDimension::duration,
                             "/preparation/preparation_duration");
                resolved.trailing_complete_cycle_count.value =
                    preparation.trailing_complete_cycle_count;
                scenario_.preparation = std::move(resolved);
            }
        },
        document_.preparation);
}

void ScenarioResolver::compile_operating_state() {
    contract::OperatingState state{
        document_.initial_state.ignition_enabled, document_.initial_state.fuel_enabled,
        document_.initial_state.starter_enabled,  document_.initial_state.dyno_enabled,
        document_.initial_state.limiter_enabled,
    };
    std::vector<contract::OperatingStatePoint> timeline{
        {"initial-state", 0.0, state},
    };
    std::optional<std::uint64_t> previous_authored_patch_frame;
    for (std::size_t index = 0; index < document_.events.size(); ++index) {
        const auto &event = document_.events[index];
        const auto event_path = "/events/" + std::to_string(index);
        const auto *patch = std::get_if<authoring::OperatingStatePatch>(&event.payload);
        if (patch == nullptr) {
            const bool free_vehicle =
                std::holds_alternative<authoring::FreeVehicleMode>(document_.mode);
            const bool drivetrain_event =
                std::holds_alternative<authoring::SelectGearEvent>(event.payload) ||
                std::holds_alternative<authoring::SetClutchEngagementEvent>(
                    event.payload) ||
                std::holds_alternative<authoring::SetServiceBrakeApplicationEvent>(
                    event.payload);
            if (free_vehicle && drivetrain_event) {
                // FreeVehicle mode compiles these into its three immutable control
                // lanes. They are not operating-state patches.
                continue;
            }
            add(authoring::DiagnosticCode::unsupported_capability,
                event_path + "/payload/type",
                "the selected mode cannot enact this gear, clutch, brake, "
                "monitoring, or lifecycle event");
            continue;
        }
        if (!contract::is_valid_semantic_id(event.id.value)) {
            add(authoring::DiagnosticCode::invalid_value, event_path + "/id",
                "executable event IDs use the canonical lowercase semantic-ID "
                "grammar");
        }
        const auto time_s = quantity(event.time, authoring::QuantityDimension::duration,
                                     event_path + "/time");
        const auto frame = physics_frame(time_s, event_path + "/time");
        if (!frame.has_value()) {
            continue;
        }
        if (*frame >= request_input_.total_physics_frames) {
            add(authoring::DiagnosticCode::unsupported_capability, event_path + "/time",
                "an event at or after the final physics step cannot be enacted");
            continue;
        }
        if (previous_authored_patch_frame.has_value() &&
            *previous_authored_patch_frame == *frame) {
            add(authoring::DiagnosticCode::unsupported_capability, event_path + "/time",
                "the current event journal cannot retain two distinct "
                "operating-state events at one physics boundary");
            continue;
        }
        previous_authored_patch_frame = frame;

        if (patch->ignition_enabled.has_value()) {
            state.ignition_enabled = *patch->ignition_enabled;
        }
        if (patch->fuel_enabled.has_value()) {
            state.fuel_enabled = *patch->fuel_enabled;
        }
        if (patch->starter_enabled.has_value()) {
            state.starter_enabled = *patch->starter_enabled;
        }
        if (patch->dyno_enabled.has_value()) {
            state.dyno_enabled = *patch->dyno_enabled;
        }
        if (patch->limiter_enabled.has_value()) {
            state.limiter_enabled = *patch->limiter_enabled;
        }
        if (*frame == 0U) {
            timeline.front() = {event.id.value, time_s, state};
        } else {
            timeline.push_back({event.id.value, time_s, state});
        }
    }
    scenario_.operating_state.value = std::move(timeline);
}

} // namespace crankwave::compile::detail::scenario_resolution
