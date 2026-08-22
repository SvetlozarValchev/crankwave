#include "telemetry_encoder_support.hpp"

#include <cmath>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>

namespace crankwave::artifacts::detail {
namespace {

TelemetryEncodingError non_finite(std::string path) {
    return {
        TelemetryEncodingErrorCode::non_finite_value,
        std::move(path),
        "telemetry serialization requires every stored floating value to be finite",
    };
}

std::optional<TelemetryEncodingError>
check_values(const std::string &base,
             std::initializer_list<std::pair<const char *, double>> values) {
    for (const auto &[name, value] : values) {
        if (!std::isfinite(value)) {
            return non_finite(base + "." + name);
        }
    }
    return std::nullopt;
}

std::optional<TelemetryEncodingError>
check_quantity(const contract::QuantityValue &value, const std::string &path) {
    return check_values(path, {{"value", value.value}});
}

std::optional<TelemetryEncodingError> check_torque(const contract::TorqueValueNm &value,
                                                   const std::string &path) {
    return check_values(path, {{"value_nm", value.value_nm}});
}

std::optional<TelemetryEncodingError>
check_torque_telemetry(const contract::TorqueTelemetry &value,
                       const std::string &path) {
    const auto check_torque_field = [&](const contract::TorqueValueNm &field,
                                        const char *name) {
        return check_torque(field, path + "." + name);
    };
    if (auto issue = check_torque_field(value.instantaneous_indicated_gas,
                                        "instantaneous_indicated_gas")) {
        return issue;
    }
    if (auto issue = check_torque_field(value.pumping_partition, "pumping_partition")) {
        return issue;
    }
    if (auto issue = check_torque_field(value.friction_pump_and_accessory,
                                        "friction_pump_and_accessory")) {
        return issue;
    }
    if (auto issue = check_torque_field(value.starter, "starter")) {
        return issue;
    }
    if (auto issue = check_torque_field(value.instantaneous_net_shaft,
                                        "instantaneous_net_shaft")) {
        return issue;
    }
    if (auto issue =
            check_torque_field(value.cycle_mean_net_shaft, "cycle_mean_net_shaft")) {
        return issue;
    }
    if (auto issue = check_torque_field(value.actuator, "actuator")) {
        return issue;
    }
    if (auto issue = check_torque_field(value.dyno_reaction, "dyno_reaction")) {
        return issue;
    }
    if (auto issue = check_quantity(value.cycle_work_j, path + ".cycle_work_j")) {
        return issue;
    }
    if (auto issue = check_quantity(value.net_bmep_pa, path + ".net_bmep_pa")) {
        return issue;
    }
    if (auto issue = check_quantity(value.instantaneous_power_w,
                                    path + ".instantaneous_power_w")) {
        return issue;
    }
    return check_quantity(value.cycle_mean_power_w, path + ".cycle_mean_power_w");
}

std::optional<TelemetryEncodingError>
check_mixture(const contract::MixtureFractions &value, const std::string &path) {
    return check_values(
        path, {{"fuel", value.fuel}, {"inert", value.inert}, {"oxygen", value.oxygen}});
}

} // namespace

std::optional<TelemetryEncodingError>
validate_all_serialized_values_finite(const contract::CaptureBlockView &block) {
    for (std::size_t index = 0; index < block.engine().size(); ++index) {
        const auto &sample = block.engine()[index];
        const auto path = "block.engine[" + std::to_string(index) + "]";
        if (auto issue = check_values(
                path,
                {
                    {"theta_rad", sample.theta_rad},
                    {"theta_cycle_rad", sample.theta_cycle_rad},
                    {"angular_speed_rad_s", sample.angular_speed_rad_s},
                    {"angular_acceleration_rad_s2", sample.angular_acceleration_rad_s2},
                    {"engine_speed_rpm", sample.engine_speed_rpm},
                    {"requested_throttle_01", sample.requested_throttle_01},
                    {"resolved_engine_throttle_01", sample.resolved_engine_throttle_01},
                    {"intake_plate_position_01", sample.intake_plate_position_01},
                    {"main_flow_multiplier_01", sample.main_flow_multiplier_01},
                })) {
            return issue;
        }
        if (auto issue = check_torque_telemetry(sample.torque, path + ".torque")) {
            return issue;
        }
    }

    for (std::size_t index = 0; index < block.cylinders().size(); ++index) {
        const auto &sample = block.cylinders()[index];
        const auto path = "block.cylinders[" + std::to_string(index) + "]";
        if (auto issue = check_values(
                path,
                {
                    {"chamber_volume_m3", sample.chamber_volume_m3},
                    {"chamber_dvolume_dtheta_m3_per_rad",
                     sample.chamber_dvolume_dtheta_m3_per_rad},
                    {"piston_velocity_m_s", sample.piston_velocity_m_s},
                    {"pressure_pa_abs", sample.pressure_pa_abs},
                    {"temperature_k", sample.temperature_k},
                    {"amount_mol", sample.amount_mol},
                    {"combustion_heat_release_j", sample.combustion_heat_release_j},
                    {"flame_radius_m", sample.flame_radius_m},
                    {"flame_axial_travel_m", sample.flame_axial_travel_m},
                })) {
            return issue;
        }
        if (auto issue = check_mixture(sample.composition, path + ".composition")) {
            return issue;
        }
        if (auto issue = check_torque(sample.indicated_gas_torque,
                                      path + ".indicated_gas_torque")) {
            return issue;
        }
    }

    for (std::size_t index = 0; index < block.ports().size(); ++index) {
        const auto &sample = block.ports()[index];
        if (auto issue = check_values(
                "block.ports[" + std::to_string(index) + "]",
                {
                    {"pressure_pa_abs", sample.pressure_pa_abs},
                    {"temperature_k", sample.temperature_k},
                    {"signed_mass_flow_kg_s", sample.signed_mass_flow_kg_s},
                    {"effective_flow_area_m2", sample.effective_flow_area_m2},
                    {"effective_molar_flow_conductance_m2_sqrt_mol_per_kg",
                     sample.effective_molar_flow_conductance_m2_sqrt_mol_per_kg},
                    {"valve_lift_m", sample.valve_lift_m},
                })) {
            return issue;
        }
    }

    for (std::size_t index = 0; index < block.gas_volumes().size(); ++index) {
        const auto &sample = block.gas_volumes()[index];
        const auto path = "block.gas_volumes[" + std::to_string(index) + "]";
        if (auto issue =
                check_values(path, {
                                       {"volume_m3", sample.volume_m3},
                                       {"pressure_pa_abs", sample.pressure_pa_abs},
                                       {"temperature_k", sample.temperature_k},
                                       {"amount_mol", sample.amount_mol},
                                       {"thermal_energy_j", sample.thermal_energy_j},
                                       {"momentum_x_kg_m_s", sample.momentum_x_kg_m_s},
                                       {"momentum_y_kg_m_s", sample.momentum_y_kg_m_s},
                                   })) {
            return issue;
        }
        if (auto issue = check_mixture(sample.composition, path + ".composition")) {
            return issue;
        }
    }

    for (std::size_t index = 0; index < block.flow_edges().size(); ++index) {
        if (auto issue =
                check_values("block.flow_edges[" + std::to_string(index) + "]",
                             {{"signed_mass_flow_kg_s",
                               block.flow_edges()[index].signed_mass_flow_kg_s}})) {
            return issue;
        }
    }

    for (std::size_t index = 0; index < block.source_routes().size(); ++index) {
        const auto path = "block.source_routes[" + std::to_string(index) + "]";
        const auto issue = std::visit(
            [&](const auto &sample) -> std::optional<TelemetryEncodingError> {
                using Sample = std::decay_t<decltype(sample)>;
                if constexpr (std::is_same_v<Sample,
                                             contract::GasSourceRouteCaptureSample>) {
                    return check_values(
                        path,
                        {
                            {"pressure_pa_abs", sample.pressure_pa_abs},
                            {"temperature_k", sample.temperature_k},
                            {"signed_mass_flow_kg_s", sample.signed_mass_flow_kg_s},
                            {"effective_area_m2", sample.effective_area_m2},
                        });
                } else {
                    return check_values(
                        path, {
                                  {"force_xyz_n[0]", sample.force_xyz_n[0]},
                                  {"force_xyz_n[1]", sample.force_xyz_n[1]},
                                  {"force_xyz_n[2]", sample.force_xyz_n[2]},
                                  {"torque_xyz_nm[0]", sample.torque_xyz_nm[0]},
                                  {"torque_xyz_nm[1]", sample.torque_xyz_nm[1]},
                                  {"torque_xyz_nm[2]", sample.torque_xyz_nm[2]},
                              });
                }
            },
            block.source_routes()[index]);
        if (issue.has_value()) {
            return issue;
        }
    }

    if (block.reference_parity().has_value()) {
        const auto &parity = *block.reference_parity();
        for (std::size_t index = 0; index < parity.filtered_engine_speed_rpm().size();
             ++index) {
            if (auto issue = check_values(
                    "block.reference_parity.filtered_engine_speed_rpm[" +
                        std::to_string(index) + "]",
                    {{"value", parity.filtered_engine_speed_rpm()[index]}})) {
                return issue;
            }
        }
        for (std::size_t index = 0; index < parity.cylinders().size(); ++index) {
            const auto &sample = parity.cylinders()[index];
            if (auto issue = check_values(
                    "block.reference_parity.cylinders[" + std::to_string(index) + "]",
                    {
                        {"exhaust_primary_static_pressure_pa_abs",
                         sample.exhaust_primary_static_pressure_pa_abs},
                        {"dynamic_pressure_forward_pa",
                         sample.dynamic_pressure_forward_pa},
                        {"dynamic_pressure_reverse_pa",
                         sample.dynamic_pressure_reverse_pa},
                    })) {
                return issue;
            }
        }
    }

    for (std::size_t index = 0; index < block.event_journal().events().size();
         ++index) {
        const auto &event = block.event_journal().events()[index];
        const auto path = "block.event_journal.events[" + std::to_string(index) + "]";
        const auto issue = std::visit(
            [&](const auto &payload) -> std::optional<TelemetryEncodingError> {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, contract::SparkCrossing>) {
                    return check_values(
                        path,
                        {
                            {"raw_saved_angle_rad", payload.raw_saved_angle_rad},
                            {"raw_current_angle_rad", payload.raw_current_angle_rad},
                            {"adjusted_current_angle_rad",
                             payload.adjusted_current_angle_rad},
                            {"adjusted_spark_angle_rad",
                             payload.adjusted_spark_angle_rad},
                            {"timing_advance_rad", payload.timing_advance_rad},
                        });
                } else if constexpr (std::is_same_v<Payload,
                                                    contract::LimiterStateChanged>) {
                    return check_values(
                        path, {{"resulting_timer_s", payload.resulting_timer_s}});
                } else if constexpr (std::is_same_v<Payload,
                                                    contract::IgnitionAccepted>) {
                    return check_values(path,
                                        {{"efficiency_01", payload.efficiency_01},
                                         {"flame_speed_m_s", payload.flame_speed_m_s}});
                } else {
                    return std::nullopt;
                }
            },
            event.payload);
        if (issue.has_value()) {
            return issue;
        }
    }

    return std::nullopt;
}

} // namespace crankwave::artifacts::detail
