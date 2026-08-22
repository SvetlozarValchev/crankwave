#include "simulation/legacy_low_order_gas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace crankwave::simulation {
namespace {

enum class CellValidity {
    valid,
    nonfinite,
    nonphysical,
};

[[nodiscard]] bool finite_mixture(const LegacyGasMixture &mixture) noexcept {
    return std::isfinite(mixture.fuel_fraction) &&
           std::isfinite(mixture.inert_fraction) &&
           std::isfinite(mixture.oxygen_fraction);
}

[[nodiscard]] bool nonnegative_mixture(const LegacyGasMixture &mixture) noexcept {
    return mixture.fuel_fraction >= 0.0 && mixture.inert_fraction >= 0.0 &&
           mixture.oxygen_fraction >= 0.0;
}

[[nodiscard]] CellValidity classify_cell(const LegacyGasCell &cell) noexcept {
    if (!std::isfinite(cell.amount_mol) || !std::isfinite(cell.thermal_energy_j) ||
        !std::isfinite(cell.volume_m3) || !std::isfinite(cell.momentum_x_kg_m_s) ||
        !std::isfinite(cell.momentum_y_kg_m_s) || !finite_mixture(cell.mixture)) {
        return CellValidity::nonfinite;
    }
    if (cell.amount_mol <= 0.0 || cell.thermal_energy_j <= 0.0 ||
        cell.volume_m3 <= 0.0 || !nonnegative_mixture(cell.mixture)) {
        return CellValidity::nonphysical;
    }

    const double pressure_pa = legacy_gas_pressure_pa(cell);
    const double temperature_k = legacy_gas_temperature_k(cell);
    const double mass_kg = legacy_gas_mass_kg(cell);
    const double density_kg_m3 = legacy_gas_density_kg_m3(cell);
    const double sonic_velocity_m_s = legacy_gas_sonic_velocity_m_s(cell);
    const double bulk_energy_j = legacy_gas_bulk_kinetic_energy_j(cell);
    if (!std::isfinite(pressure_pa) || !std::isfinite(temperature_k) ||
        !std::isfinite(mass_kg) || !std::isfinite(density_kg_m3) ||
        !std::isfinite(sonic_velocity_m_s) || !std::isfinite(bulk_energy_j)) {
        return CellValidity::nonfinite;
    }
    if (pressure_pa <= 0.0 || temperature_k <= 0.0 || mass_kg <= 0.0 ||
        density_kg_m3 <= 0.0 || sonic_velocity_m_s <= 0.0 || bulk_energy_j < 0.0) {
        return CellValidity::nonphysical;
    }
    return CellValidity::valid;
}

[[nodiscard]] std::string summarize_cell(const LegacyGasCell &cell,
                                         std::string_view operation,
                                         std::uint32_t gas_substep_index) {
    std::ostringstream summary;
    summary << "operation=" << operation << "; gas_substep=" << gas_substep_index
            << "; n_mol=" << cell.amount_mol << "; U_j=" << cell.thermal_energy_j
            << "; V_m3=" << cell.volume_m3
            << "; pressure_pa=" << legacy_gas_pressure_pa(cell)
            << "; temperature_k=" << legacy_gas_temperature_k(cell)
            << "; momentum_x=" << cell.momentum_x_kg_m_s
            << "; momentum_y=" << cell.momentum_y_kg_m_s
            << "; mixture=" << cell.mixture.fuel_fraction << ','
            << cell.mixture.inert_fraction << ',' << cell.mixture.oxygen_fraction;
    return summary.str();
}

[[nodiscard]] bool finite_flame(const LegacyFlameState &flame) noexcept {
    return std::isfinite(flame.lit_amount_mol) &&
           std::isfinite(flame.diagnostic_total_amount_mol) &&
           std::isfinite(flame.percentage_lit_01) &&
           std::isfinite(flame.efficiency_01) && std::isfinite(flame.flame_speed_m_s) &&
           std::isfinite(flame.last_volume_m3) &&
           std::isfinite(flame.radial_travel_m) &&
           std::isfinite(flame.axial_travel_m) && finite_mixture(flame.global_mixture);
}

[[nodiscard]] bool physical_flame(const LegacyFlameState &flame) noexcept {
    return flame.lit_amount_mol >= 0.0 && flame.diagnostic_total_amount_mol >= 0.0 &&
           flame.percentage_lit_01 >= 0.0 && flame.efficiency_01 >= 0.0 &&
           flame.efficiency_01 <= 1.0 && flame.flame_speed_m_s >= 0.0 &&
           flame.last_volume_m3 >= 0.0 && flame.radial_travel_m >= 0.0 &&
           flame.axial_travel_m >= 0.0 &&
           (!flame.active || flame.last_volume_m3 > 0.0) &&
           nonnegative_mixture(flame.global_mixture);
}

[[nodiscard]] bool
finite_mechanics_scalars(const LegacyMechanismStep &mechanics) noexcept {
    return std::isfinite(mechanics.requested_throttle_01) &&
           std::isfinite(mechanics.resolved_engine_throttle_01) &&
           std::isfinite(mechanics.intake_plate_position_01) &&
           std::isfinite(mechanics.main_flow_multiplier_01) &&
           std::isfinite(mechanics.engine_speed_rpm) &&
           std::isfinite(mechanics.omega_legacy_rad_s) &&
           std::isfinite(mechanics.angular_speed_rad_s) &&
           std::isfinite(mechanics.angular_acceleration_rad_s2) &&
           std::isfinite(mechanics.body_angle_psi_rad) &&
           std::isfinite(mechanics.theta_cycle_rad) &&
           std::isfinite(mechanics.theta_unwrapped_rad) &&
           std::isfinite(mechanics.filtered_engine_speed_rpm) &&
           std::isfinite(mechanics.timing_advance_rad) &&
           std::isfinite(mechanics.limiter_timer_s);
}

[[nodiscard]] contract::IgnitionRejection
contract_rejection(LegacyIgnitionDisposition disposition) noexcept {
    switch (disposition) {
    case LegacyIgnitionDisposition::rejected_active_flame:
        return contract::IgnitionRejection::active_flame;
    case LegacyIgnitionDisposition::rejected_no_fuel:
        return contract::IgnitionRejection::no_fuel;
    case LegacyIgnitionDisposition::rejected_mixture_low:
        return contract::IgnitionRejection::mixture_low;
    case LegacyIgnitionDisposition::rejected_mixture_high:
        return contract::IgnitionRejection::mixture_high;
    case LegacyIgnitionDisposition::accepted:
        break;
    }
    return contract::IgnitionRejection::unspecified;
}

} // namespace

LegacyGasolineFuelParameters
LegacyLowOrderGasSession::FuelModel::view() const noexcept {
    return {
        molecular_mass_kg_per_mol,
        energy_density_j_per_kg,
        molecular_afr,
        maximum_burning_efficiency_01,
        burning_efficiency_randomness_01,
        low_efficiency_attenuation_01,
        maximum_turbulence_effect,
        maximum_dilution_effect,
        lbv_multiplier,
        turbulence_to_flame_speed_ratio_triangle_radius,
        turbulence_to_flame_speed_ratio,
    };
}

contract::FailureContext
LegacyLowOrderGasSession::fault(contract::FailureKind kind, std::string detail_code,
                                std::string state_summary,
                                std::optional<contract::CylinderId> cylinder_id,
                                std::optional<contract::PortId> port_id,
                                std::optional<contract::GasVolumeId> gas_volume_id,
                                std::optional<contract::FlowEdgeId> flow_edge_id,
                                std::optional<contract::RouteId> route_id) const {
    return {
        kind,
        std::move(detail_code),
        model_id_,
        profile_id_,
        step_.sample_index,
        step_.step_end_index,
        static_cast<double>(step_.step_end_index) *
            static_cast<double>(rate_.denominator) /
            static_cast<double>(rate_.numerator),
        current_theta_unwrapped_rad_,
        engine_id_,
        cylinder_id,
        port_id,
        gas_volume_id,
        flow_edge_id,
        route_id,
        "scenario=" + scenario_id_ + "; " + std::move(state_summary),
        "none; simulation terminated without fallback",
        {},
    };
}

bool LegacyLowOrderGasSession::append_event(
    const contract::EngineEventPayload &payload) {
    if (step_.events.size() >= maximum_event_count_ ||
        step_.events.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max())) {
        terminal_fault_ = fault(contract::FailureKind::event_schedule_violation,
                                "legacy-gas-event-capacity-exceeded",
                                "event append exceeded the compiled per-step capacity");
        return false;
    }
    step_.events.push_back({
        static_cast<std::uint8_t>(step_.events.size()),
        payload,
    });
    return true;
}

bool LegacyLowOrderGasSession::validate_cell(
    std::size_t gas_volume_index, std::string_view operation,
    std::uint32_t gas_substep_index, std::optional<contract::CylinderId> cylinder_id,
    std::optional<contract::FlowEdgeId> flow_edge_id,
    std::optional<contract::RouteId> route_id) {
    if (gas_volume_index >= step_.gas_volumes.size()) {
        terminal_fault_ =
            fault(contract::FailureKind::contract_violation,
                  "legacy-gas-volume-index-invalid",
                  "compiled gas-volume index escaped the public state layout",
                  cylinder_id, std::nullopt, std::nullopt, flow_edge_id, route_id);
        return false;
    }
    const auto &volume = step_.gas_volumes[gas_volume_index];
    const CellValidity validity = classify_cell(volume.cell);
    if (validity == CellValidity::valid) {
        return true;
    }
    terminal_fault_ = fault(
        validity == CellValidity::nonfinite ? contract::FailureKind::numerical_failure
                                            : contract::FailureKind::nonphysical_state,
        validity == CellValidity::nonfinite ? "legacy-gas-nonfinite-state"
                                            : "legacy-gas-nonphysical-state",
        summarize_cell(volume.cell, operation, gas_substep_index), cylinder_id,
        std::nullopt, volume.gas_volume_id, flow_edge_id, route_id);
    return false;
}

LegacyGasAdvanceResult
LegacyLowOrderGasSession::advance(const LegacyMechanismStep &mechanics) {
    if (terminal_fault_.has_value()) {
        return *terminal_fault_;
    }
    if (produced_sample_count_ == std::numeric_limits<std::uint64_t>::max()) {
        terminal_fault_ =
            fault(contract::FailureKind::contract_violation,
                  "legacy-gas-frame-counter-overflow",
                  "open-ended gas execution exhausted its uint64 physics clock");
        return *terminal_fault_;
    }

    step_.rate = rate_;
    step_.sample_index = mechanics.sample_index;
    step_.step_end_index = mechanics.step_end_index;
    step_.timestamp_tick = mechanics.timestamp_tick;
    step_.events.clear();
    current_theta_unwrapped_rad_ = std::isfinite(mechanics.theta_unwrapped_rad)
                                       ? mechanics.theta_unwrapped_rad
                                       : 0.0;

    const std::uint64_t expected_sample_index =
        first_sample_index_ + produced_sample_count_;
    if ((expected_sample_count_.has_value() &&
         produced_sample_count_ >= *expected_sample_count_) ||
        mechanics.rate != rate_ || mechanics.sample_index != expected_sample_index ||
        mechanics.step_end_index != mechanics.sample_index + 1U ||
        mechanics.timestamp_tick != mechanics.step_end_index ||
        mechanics.cylinders.size() != cylinders_.size() ||
        mechanics.cylinders.size() != step_.cylinders.size() ||
        !finite_mechanics_scalars(mechanics)) {
        terminal_fault_ = fault(
            contract::FailureKind::contract_violation,
            "legacy-gas-mechanics-step-mismatch",
            "mechanics input did not match the compiled gas-session clock, shape, "
            "or finite-state contract");
        return *terminal_fault_;
    }

    for (std::size_t index = 0; index < cylinders_.size(); ++index) {
        const auto &mechanism_cylinder = mechanics.cylinders[index];
        const auto &public_cylinder =
            step_.cylinders[cylinders_[index].public_cylinder_index];
        if (mechanism_cylinder.cylinder_id != public_cylinder.cylinder_id ||
            mechanism_cylinder.exhaust_route_id != public_cylinder.exhaust_route_id ||
            !std::isfinite(mechanism_cylinder.chamber_volume_m3) ||
            mechanism_cylinder.chamber_volume_m3 <= 0.0 ||
            !std::isfinite(mechanism_cylinder.dvolume_dtheta_m3_per_rad) ||
            !std::isfinite(mechanism_cylinder.piston_speed_abs_m_s) ||
            mechanism_cylinder.piston_speed_abs_m_s < 0.0) {
            terminal_fault_ =
                fault(contract::FailureKind::contract_violation,
                      "legacy-gas-mechanics-cylinder-mismatch",
                      "mechanics cylinder identity, route, or analytic state did not "
                      "match the compiled gas lane",
                      public_cylinder.cylinder_id, std::nullopt,
                      public_cylinder.chamber_volume_id, std::nullopt,
                      public_cylinder.exhaust_route_id);
            return *terminal_fault_;
        }
    }

    expected_spark_cylinders_.clear();
    for (const auto &cylinder : mechanics.cylinders) {
        if (cylinder.spark_crossed) {
            expected_spark_cylinders_.push_back(cylinder.cylinder_id);
        }
    }
    if ((!mechanics.operating_state.ignition_enabled || previous_limiter_cut_active_) &&
        !expected_spark_cylinders_.empty()) {
        terminal_fault_ = fault(
            contract::FailureKind::event_schedule_violation,
            "legacy-gas-mechanics-event-invalid",
            "mechanics marked a spark while ignition or the preceding limiter state "
            "disabled crossing tests");
        return *terminal_fault_;
    }

    std::size_t spark_event_count = 0;
    bool limiter_event_seen = false;
    for (std::size_t index = 0; index < mechanics.events.size(); ++index) {
        const auto &event = mechanics.events[index];
        bool valid = event.ordinal_within_step == static_cast<std::uint8_t>(index);
        if (const auto *spark = std::get_if<contract::SparkCrossing>(&event.payload)) {
            valid =
                valid && !limiter_event_seen &&
                spark_event_count < expected_spark_cylinders_.size() &&
                spark->cylinder_id == expected_spark_cylinders_[spark_event_count] &&
                std::isfinite(spark->raw_saved_angle_rad) &&
                std::isfinite(spark->raw_current_angle_rad) &&
                std::isfinite(spark->adjusted_current_angle_rad) &&
                std::isfinite(spark->adjusted_spark_angle_rad) &&
                std::isfinite(spark->timing_advance_rad) &&
                spark->raw_current_angle_rad == mechanics.theta_cycle_rad &&
                spark->timing_advance_rad == mechanics.timing_advance_rad;
            ++spark_event_count;
        } else if (const auto *limiter =
                       std::get_if<contract::LimiterStateChanged>(&event.payload)) {
            valid = valid && !limiter_event_seen &&
                    spark_event_count == expected_spark_cylinders_.size() &&
                    limiter->old_active == previous_limiter_cut_active_ &&
                    limiter->new_active == mechanics.limiter_cut_active &&
                    limiter->old_active != limiter->new_active &&
                    std::isfinite(limiter->resulting_timer_s) &&
                    limiter->resulting_timer_s >= 0.0 &&
                    limiter->resulting_timer_s == mechanics.limiter_timer_s;
            limiter_event_seen = true;
        } else {
            valid = false;
        }
        if (!valid || !append_event(event.payload)) {
            if (!terminal_fault_.has_value()) {
                terminal_fault_ =
                    fault(contract::FailureKind::event_schedule_violation,
                          "legacy-gas-mechanics-event-invalid",
                          "mechanics event journal disagreed with spark flags, limiter "
                          "state, cylinder order, or append order");
            }
            return *terminal_fault_;
        }
    }
    if (spark_event_count != expected_spark_cylinders_.size() ||
        (!limiter_event_seen &&
         mechanics.limiter_cut_active != previous_limiter_cut_active_)) {
        terminal_fault_ = fault(
            contract::FailureKind::event_schedule_violation,
            "legacy-gas-mechanics-event-invalid",
            "mechanics spark flags or limiter transition were missing from the event "
            "journal");
        return *terminal_fault_;
    }
    previous_limiter_cut_active_ = mechanics.limiter_cut_active;

    double left_boundary_manifold_pressure_pa_abs = legacy_gas_pressure_pa(
        step_.gas_volumes[intakes_.front().plenum_volume_index].cell);
    if (intakes_.size() > 1) {
        for (std::size_t index = 1; index < intakes_.size(); ++index) {
            left_boundary_manifold_pressure_pa_abs += legacy_gas_pressure_pa(
                step_.gas_volumes[intakes_[index].plenum_volume_index].cell);
        }
        left_boundary_manifold_pressure_pa_abs /= static_cast<double>(intakes_.size());
    }
    const LegacyVtecSelectorInput valvetrain_selection_input{
        mechanics.omega_legacy_rad_s,
        left_boundary_manifold_pressure_pa_abs,
        1.0 - mechanics.resolved_engine_throttle_01,
    };
    const auto fuel = fuel_.view();

    // Ignition consumes the old thermodynamic state but snapshots the current
    // post-mechanism geometric volume. It precedes chamber volume work and history.
    for (std::size_t index = 0; index < cylinders_.size(); ++index) {
        auto &lane = cylinders_[index];
        const auto &mechanism_cylinder = mechanics.cylinders[index];
        auto &public_cylinder = step_.cylinders[lane.public_cylinder_index];
        auto &chamber = step_.gas_volumes[lane.chamber_volume_index].cell;
        if (!mechanism_cylinder.spark_crossed) {
            continue;
        }

        const auto ignition = legacy_try_ignite(
            public_cylinder.flame, lane.random, chamber,
            mechanism_cylinder.chamber_volume_m3, lane.piston_speed_history_m_s,
            lane.pressure_history_pa, fuel);
        if (ignition.accepted()) {
            if (!append_event(contract::IgnitionAccepted{public_cylinder.cylinder_id,
                                                         ignition.efficiency_01,
                                                         ignition.flame_speed_m_s})) {
                return *terminal_fault_;
            }
        } else if (!append_event(contract::IgnitionRejected{
                       public_cylinder.cylinder_id,
                       contract_rejection(ignition.disposition)})) {
            return *terminal_fault_;
        }

        if (!finite_flame(public_cylinder.flame) ||
            !physical_flame(public_cylinder.flame)) {
            terminal_fault_ = fault(!finite_flame(public_cylinder.flame)
                                        ? contract::FailureKind::numerical_failure
                                        : contract::FailureKind::nonphysical_state,
                                    !finite_flame(public_cylinder.flame)
                                        ? "legacy-gas-nonfinite-flame"
                                        : "legacy-gas-nonphysical-flame",
                                    "ignition produced an invalid flame snapshot",
                                    public_cylinder.cylinder_id, std::nullopt,
                                    public_cylinder.chamber_volume_id);
            return *terminal_fault_;
        }
    }

    // Chamber work, angle-indexed history, then the outer-step valve sample.
    for (std::size_t index = 0; index < cylinders_.size(); ++index) {
        auto &lane = cylinders_[index];
        const auto &mechanism_cylinder = mechanics.cylinders[index];
        auto &public_cylinder = step_.cylinders[lane.public_cylinder_index];
        auto &chamber = step_.gas_volumes[lane.chamber_volume_index].cell;

        legacy_set_gas_volume(chamber, mechanism_cylinder.chamber_volume_m3);
        if (!validate_cell(lane.chamber_volume_index, "outer-volume-work", 0U,
                           public_cylinder.cylinder_id)) {
            return *terminal_fault_;
        }

        const double history_coordinate =
            mechanics.theta_cycle_rad / (4.0 * kLegacyPi) *
            static_cast<double>(kLegacyCombustionHistorySampleCount - 1U);
        const double rounded_history_index = std::round(history_coordinate);
        if (!std::isfinite(rounded_history_index) || rounded_history_index < 0.0 ||
            rounded_history_index >=
                static_cast<double>(kLegacyCombustionHistorySampleCount)) {
            terminal_fault_ =
                fault(contract::FailureKind::contract_violation,
                      "legacy-gas-history-index-invalid",
                      "wrapped cycle angle produced an invalid 256-bin history index",
                      public_cylinder.cylinder_id, std::nullopt,
                      public_cylinder.chamber_volume_id);
            return *terminal_fault_;
        }
        const auto history_index = static_cast<std::size_t>(rounded_history_index);
        lane.piston_speed_history_m_s[history_index] =
            mechanism_cylinder.piston_speed_abs_m_s;
        lane.pressure_history_pa[history_index] = legacy_gas_pressure_pa(chamber);

        const auto &active_valvetrain =
            valvetrain_->profile_for(index, valvetrain_selection_input);
        const auto valve_sample =
            active_valvetrain.sample_cylinder(index, mechanics.body_angle_psi_rad);
        if (!valve_sample.has_value() ||
            valve_sample->cylinder_id != public_cylinder.cylinder_id ||
            valve_sample->intake_port_id != public_cylinder.intake_port_id ||
            valve_sample->exhaust_port_id != public_cylinder.exhaust_port_id) {
            terminal_fault_ = fault(
                contract::FailureKind::contract_violation,
                "legacy-gas-valvetrain-sample-invalid",
                "admitted fixed valvetrain did not produce the bound cylinder and "
                "port sample",
                public_cylinder.cylinder_id, public_cylinder.intake_port_id,
                public_cylinder.chamber_volume_id);
            return *terminal_fault_;
        }
        public_cylinder.valves = *valve_sample;
    }

    for (auto &edge : step_.flow_edges) {
        edge.signed_amount_mol = 0.0;
    }
    for (auto &cylinder : step_.cylinders) {
        cylinder.outer_step_combustion_heat_release_j = 0.0;
        cylinder.latest_signed_intake_transfer_mol = 0.0;
        cylinder.latest_signed_exhaust_transfer_mol = 0.0;
    }

    const auto validate_work_cell =
        [&](const LegacyGasCell &cell, std::string_view operation,
            std::uint32_t gas_substep_index,
            std::optional<contract::FlowEdgeId> edge_id = std::nullopt,
            std::optional<contract::RouteId> route_id = std::nullopt) {
            const CellValidity validity = classify_cell(cell);
            if (validity == CellValidity::valid) {
                return true;
            }
            terminal_fault_ =
                fault(validity == CellValidity::nonfinite
                          ? contract::FailureKind::numerical_failure
                          : contract::FailureKind::nonphysical_state,
                      validity == CellValidity::nonfinite
                          ? "legacy-gas-nonfinite-boundary-state"
                          : "legacy-gas-nonphysical-boundary-state",
                      summarize_cell(cell, operation, gas_substep_index), std::nullopt,
                      std::nullopt, std::nullopt, edge_id, route_id);
            return false;
        };

    for (std::uint32_t substep = 0; substep < kLegacyGasSubstepCount; ++substep) {
        // Exhaust collectors precede every intake and every cylinder.
        for (auto &route : routes_) {
            auto &collector = step_.gas_volumes[route.collector_volume_index].cell;
            auto &edge = step_.flow_edges[route.collector_outlet_edge_index];
            const auto &public_route = step_.exhaust_routes[route.public_route_index];
            const auto route_id = public_route.route_id;

            legacy_reset_gas_cell(route.atmosphere_work_cell, ambient_pressure_pa_,
                                  ambient_temperature_k_, inert_mixture_);
            if (!validate_work_cell(route.atmosphere_work_cell,
                                    "collector-atmosphere-reset", substep,
                                    edge.flow_edge_id, route_id)) {
                return *terminal_fault_;
            }
            const auto outlet =
                legacy_transfer_gas(route.atmosphere_work_cell, collector,
                                    LegacyFiniteGasTransferParameters{
                                        route.collector_outlet_k,
                                        gas_step_s_,
                                        1.0,
                                        0.0,
                                        public_route.collector_cross_section_area_m2,
                                        10.0,
                                    });
            edge.signed_amount_mol += outlet.signed_amount_mol;
            if (!validate_work_cell(route.atmosphere_work_cell, "collector-outlet-flow",
                                    substep, edge.flow_edge_id, route_id) ||
                !validate_cell(route.collector_volume_index, "collector-outlet-flow",
                               substep, std::nullopt, edge.flow_edge_id, route_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(collector);
            if (!validate_cell(route.collector_volume_index, "collector-sonic-bound",
                               substep, std::nullopt, edge.flow_edge_id, route_id)) {
                return *terminal_fault_;
            }
            legacy_apply_gas_self_impulse(
                collector, step_.gas_volumes[route.collector_volume_index].geometry,
                gas_step_s_, route.velocity_decay);
            if (!validate_cell(route.collector_volume_index, "collector-self-impulse",
                               substep, std::nullopt, edge.flow_edge_id, route_id)) {
                return *terminal_fault_;
            }
        }

        for (auto &intake : intakes_) {
            auto &plenum = step_.gas_volumes[intake.plenum_volume_index].cell;
            auto &main_edge = step_.flow_edges[intake.main_throttle_edge_index];
            auto &idle_edge = step_.flow_edges[intake.idle_bypass_edge_index];

            const double ideal_afr =
                (intake.main_mixture_lambda * fuel_.molecular_afr) * 4.0;
            const double main_air_fraction = mechanics.operating_state.fuel_enabled
                                                 ? ideal_afr / (1.0 + ideal_afr)
                                                 : 1.0;
            LegacyGasMixture main_mixture;
            main_mixture.fuel_fraction = 1.0 - main_air_fraction;
            main_mixture.inert_fraction = main_air_fraction * 0.75;
            main_mixture.oxygen_fraction = main_air_fraction * 0.25;
            legacy_reset_gas_cell(intake.atmosphere_work_cell, ambient_pressure_pa_,
                                  ambient_temperature_k_, main_mixture);
            if (!validate_work_cell(intake.atmosphere_work_cell,
                                    "main-atmosphere-reset", substep,
                                    main_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            const double intake_plate_position_01 =
                intake.idle_throttle_plate_position_01 *
                mechanics.resolved_engine_throttle_01;
            const double main_flow_multiplier_01 =
                intakes_.size() == 1
                    ? mechanics.main_flow_multiplier_01
                    : std::cos(kLegacyPi * intake_plate_position_01 / 2.0);
            const auto main_flow = legacy_transfer_gas(
                intake.atmosphere_work_cell, plenum,
                LegacyFiniteGasTransferParameters{
                    main_flow_multiplier_01 * intake.main_throttle_k,
                    gas_step_s_,
                    0.0,
                    -1.0,
                    10.0,
                    intake.plenum_cross_section_area_m2,
                });
            main_edge.signed_amount_mol += main_flow.signed_amount_mol;
            if (!validate_work_cell(intake.atmosphere_work_cell, "main-throttle-flow",
                                    substep, main_edge.flow_edge_id) ||
                !validate_cell(intake.plenum_volume_index, "main-throttle-flow",
                               substep, std::nullopt, main_edge.flow_edge_id)) {
                return *terminal_fault_;
            }

            const double idle_afr = 2.0;
            const double idle_air_fraction = mechanics.operating_state.fuel_enabled
                                                 ? idle_afr / (1.0 + idle_afr)
                                                 : 1.0;
            LegacyGasMixture idle_mixture;
            idle_mixture.fuel_fraction = 1.0 - idle_air_fraction;
            idle_mixture.inert_fraction = idle_air_fraction * 0.75;
            idle_mixture.oxygen_fraction = idle_air_fraction * 0.25;
            legacy_reset_gas_cell(intake.atmosphere_work_cell, ambient_pressure_pa_,
                                  ambient_temperature_k_, idle_mixture);
            if (!validate_work_cell(intake.atmosphere_work_cell,
                                    "idle-atmosphere-reset", substep,
                                    idle_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            const auto idle_flow =
                legacy_transfer_gas(intake.atmosphere_work_cell, plenum,
                                    LegacyFiniteGasTransferParameters{
                                        intake.idle_bypass_k,
                                        gas_step_s_,
                                        0.0,
                                        -1.0,
                                        10.0,
                                        intake.plenum_cross_section_area_m2,
                                    });
            idle_edge.signed_amount_mol += idle_flow.signed_amount_mol;
            if (!validate_work_cell(intake.atmosphere_work_cell, "idle-bypass-flow",
                                    substep, idle_edge.flow_edge_id) ||
                !validate_cell(intake.plenum_volume_index, "idle-bypass-flow", substep,
                               std::nullopt, idle_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(plenum);
            if (!validate_cell(intake.plenum_volume_index, "plenum-sonic-bound",
                               substep)) {
                return *terminal_fault_;
            }
            legacy_apply_gas_self_impulse(
                plenum, step_.gas_volumes[intake.plenum_volume_index].geometry,
                gas_step_s_, intake.velocity_decay);
            if (!validate_cell(intake.plenum_volume_index, "plenum-self-impulse",
                               substep)) {
                return *terminal_fault_;
            }
        }

        for (auto &lane : cylinders_) {
            auto &public_cylinder = step_.cylinders[lane.public_cylinder_index];
            auto &intake = intakes_[lane.intake_lane_index];
            auto &plenum = step_.gas_volumes[intake.plenum_volume_index].cell;
            auto &runner = step_.gas_volumes[lane.intake_runner_volume_index].cell;
            auto &chamber = step_.gas_volumes[lane.chamber_volume_index].cell;
            auto &primary = step_.gas_volumes[lane.exhaust_primary_volume_index].cell;
            auto &route = routes_[lane.route_lane_index];
            const auto &public_route = step_.exhaust_routes[route.public_route_index];
            auto &collector = step_.gas_volumes[route.collector_volume_index].cell;

            const double pre_heat_temperature_k = legacy_gas_temperature_k(chamber);
            if (pre_heat_temperature_k > public_cylinder.peak_temperature_k) {
                public_cylinder.peak_temperature_k = pre_heat_temperature_k;
            }
            const double cylinder_height_m = chamber.volume_m3 / lane.piston_area_m2;
            const double wall_area_m2 =
                cylinder_height_m * kLegacyPi * lane.bore_m + lane.piston_area_m2 * 2.0;
            const double temperature_delta_k =
                wall_temperature_k_ - legacy_gas_temperature_k(chamber);
            const double wall_heat_j =
                temperature_delta_k * wall_area_m2 * 100.0 * gas_step_s_;
            legacy_add_gas_thermal_energy(chamber, wall_heat_j);
            if (!validate_cell(lane.chamber_volume_index, "cylinder-wall-heat", substep,
                               public_cylinder.cylinder_id)) {
                return *terminal_fault_;
            }

            auto &blowby_edge = step_.flow_edges[lane.blowby_edge_index];
            const auto blowby = legacy_transfer_gas_environment(
                chamber,
                LegacyEnvironmentGasTransferParameters{
                    lane.blowby_k,
                    gas_step_s_,
                    LegacyGasEnvironment{crankcase_pressure_pa_,
                                         crankcase_temperature_k_, inert_mixture_},
                });
            blowby_edge.signed_amount_mol += blowby.signed_amount_mol;
            if (!validate_cell(lane.chamber_volume_index, "cylinder-blowby", substep,
                               public_cylinder.cylinder_id, blowby_edge.flow_edge_id)) {
                return *terminal_fault_;
            }

            auto &plenum_runner_edge =
                step_.flow_edges[lane.plenum_to_runner_edge_index];
            const auto plenum_runner =
                legacy_transfer_gas(plenum, runner,
                                    LegacyFiniteGasTransferParameters{
                                        intake.plenum_to_runner_k,
                                        gas_step_s_,
                                        1.0,
                                        0.0,
                                        intake.plenum_cross_section_area_m2,
                                        lane.intake_runner_cross_section_area_m2,
                                    });
            plenum_runner_edge.signed_amount_mol += plenum_runner.signed_amount_mol;
            if (!validate_cell(intake.plenum_volume_index, "plenum-to-runner-flow",
                               substep, public_cylinder.cylinder_id,
                               plenum_runner_edge.flow_edge_id) ||
                !validate_cell(lane.intake_runner_volume_index, "plenum-to-runner-flow",
                               substep, public_cylinder.cylinder_id,
                               plenum_runner_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(runner);
            if (!validate_cell(lane.intake_runner_volume_index,
                               "runner-sonic-bound-before-intake", substep,
                               public_cylinder.cylinder_id)) {
                return *terminal_fault_;
            }

            const double cylinder_cross_section_area_m2 =
                chamber.volume_m3 / cylinder_height_m;
            auto &intake_edge = step_.flow_edges[lane.intake_valve_edge_index];
            const auto intake_flow =
                legacy_transfer_gas(runner, chamber,
                                    LegacyFiniteGasTransferParameters{
                                        public_cylinder.valves.intake_valve_k,
                                        gas_step_s_,
                                        1.0,
                                        0.0,
                                        lane.intake_runner_cross_section_area_m2,
                                        cylinder_cross_section_area_m2,
                                    });
            intake_edge.signed_amount_mol += intake_flow.signed_amount_mol;
            public_cylinder.latest_signed_intake_transfer_mol =
                intake_flow.signed_amount_mol;
            if (!validate_cell(lane.intake_runner_volume_index, "intake-valve-flow",
                               substep, public_cylinder.cylinder_id,
                               intake_edge.flow_edge_id) ||
                !validate_cell(lane.chamber_volume_index, "intake-valve-flow", substep,
                               public_cylinder.cylinder_id, intake_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(runner);
            if (!validate_cell(lane.intake_runner_volume_index,
                               "runner-sonic-bound-after-intake", substep,
                               public_cylinder.cylinder_id, intake_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(chamber);
            if (!validate_cell(lane.chamber_volume_index,
                               "cylinder-sonic-bound-after-intake", substep,
                               public_cylinder.cylinder_id, intake_edge.flow_edge_id)) {
                return *terminal_fault_;
            }

            auto &exhaust_edge = step_.flow_edges[lane.exhaust_valve_edge_index];
            const auto exhaust_flow =
                legacy_transfer_gas(chamber, primary,
                                    LegacyFiniteGasTransferParameters{
                                        public_cylinder.valves.exhaust_valve_k,
                                        gas_step_s_,
                                        1.0,
                                        0.0,
                                        cylinder_cross_section_area_m2,
                                        lane.exhaust_primary_cross_section_area_m2,
                                    });
            exhaust_edge.signed_amount_mol += exhaust_flow.signed_amount_mol;
            public_cylinder.latest_signed_exhaust_transfer_mol =
                exhaust_flow.signed_amount_mol;
            if (!validate_cell(lane.chamber_volume_index, "exhaust-valve-flow", substep,
                               public_cylinder.cylinder_id,
                               exhaust_edge.flow_edge_id) ||
                !validate_cell(lane.exhaust_primary_volume_index, "exhaust-valve-flow",
                               substep, public_cylinder.cylinder_id,
                               exhaust_edge.flow_edge_id,
                               public_cylinder.exhaust_route_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(chamber);
            if (!validate_cell(
                    lane.chamber_volume_index, "cylinder-sonic-bound-after-exhaust",
                    substep, public_cylinder.cylinder_id, exhaust_edge.flow_edge_id)) {
                return *terminal_fault_;
            }
            legacy_limit_gas_to_sonic_velocity(primary);
            if (!validate_cell(lane.exhaust_primary_volume_index, "primary-sonic-bound",
                               substep, public_cylinder.cylinder_id,
                               exhaust_edge.flow_edge_id,
                               public_cylinder.exhaust_route_id)) {
                return *terminal_fault_;
            }

            auto &primary_collector_edge =
                step_.flow_edges[lane.primary_to_collector_edge_index];
            const auto primary_collector =
                legacy_transfer_gas(primary, collector,
                                    LegacyFiniteGasTransferParameters{
                                        route.primary_to_collector_k,
                                        gas_step_s_,
                                        1.0,
                                        0.0,
                                        lane.exhaust_primary_cross_section_area_m2,
                                        public_route.collector_cross_section_area_m2,
                                    });
            primary_collector_edge.signed_amount_mol +=
                primary_collector.signed_amount_mol;
            if (!validate_cell(lane.exhaust_primary_volume_index,
                               "primary-to-collector-flow", substep,
                               public_cylinder.cylinder_id,
                               primary_collector_edge.flow_edge_id,
                               public_cylinder.exhaust_route_id) ||
                !validate_cell(
                    route.collector_volume_index, "primary-to-collector-flow", substep,
                    public_cylinder.cylinder_id, primary_collector_edge.flow_edge_id,
                    public_cylinder.exhaust_route_id)) {
                return *terminal_fault_;
            }

            legacy_apply_gas_self_impulse(
                runner, step_.gas_volumes[lane.intake_runner_volume_index].geometry,
                gas_step_s_, intake.velocity_decay);
            if (!validate_cell(lane.intake_runner_volume_index, "runner-self-impulse",
                               substep, public_cylinder.cylinder_id)) {
                return *terminal_fault_;
            }
            legacy_apply_gas_self_impulse(
                chamber, step_.gas_volumes[lane.chamber_volume_index].geometry,
                gas_step_s_, 0.5);
            if (!validate_cell(lane.chamber_volume_index, "cylinder-self-impulse",
                               substep, public_cylinder.cylinder_id)) {
                return *terminal_fault_;
            }
            legacy_apply_gas_self_impulse(
                primary, step_.gas_volumes[lane.exhaust_primary_volume_index].geometry,
                gas_step_s_, route.velocity_decay);
            if (!validate_cell(lane.exhaust_primary_volume_index,
                               "primary-self-impulse", substep,
                               public_cylinder.cylinder_id, std::nullopt,
                               public_cylinder.exhaust_route_id)) {
                return *terminal_fault_;
            }

            legacy_apply_gas_velocity_decay(chamber, gas_step_s_, 0.01);
            if (!validate_cell(lane.chamber_volume_index, "cylinder-velocity-decay",
                               substep, public_cylinder.cylinder_id)) {
                return *terminal_fault_;
            }
            legacy_apply_gas_velocity_decay(primary, gas_step_s_, 0.01);
            if (!validate_cell(lane.exhaust_primary_volume_index,
                               "primary-velocity-decay", substep,
                               public_cylinder.cylinder_id, std::nullopt,
                               public_cylinder.exhaust_route_id)) {
                return *terminal_fault_;
            }

            if (legacy_extinguish_flame_for_intake_transfer(
                    public_cylinder.flame, intake_flow.signed_amount_mol)) {
                if (!append_event(contract::FlameExtinguished{
                        public_cylinder.cylinder_id, static_cast<std::uint8_t>(substep),
                        contract::FlameExtinctionReason::intake_transfer})) {
                    return *terminal_fault_;
                }
            }

            const auto combustion = legacy_advance_gasoline_flame(
                public_cylinder.flame, chamber, gas_step_s_, lane.bore_m,
                lane.piston_area_m2, fuel);
            public_cylinder.outer_step_combustion_heat_release_j +=
                combustion.reaction.energy_release_j;
            public_cylinder.cumulative_burned_fuel_mass_kg +=
                combustion.reaction.burned_fuel_mass_kg;
            if (combustion.disposition ==
                LegacyFlameAdvanceDisposition::extinguished_no_geometric_progress) {
                if (!append_event(contract::FlameExtinguished{
                        public_cylinder.cylinder_id, static_cast<std::uint8_t>(substep),
                        contract::FlameExtinctionReason::no_geometric_progress})) {
                    return *terminal_fault_;
                }
            }
            if (!validate_cell(lane.chamber_volume_index, "gasoline-combustion",
                               substep, public_cylinder.cylinder_id)) {
                return *terminal_fault_;
            }
            const bool finite_combustion_state =
                finite_flame(public_cylinder.flame) &&
                std::isfinite(public_cylinder.outer_step_combustion_heat_release_j) &&
                std::isfinite(public_cylinder.cumulative_burned_fuel_mass_kg);
            const bool physical_combustion_state =
                physical_flame(public_cylinder.flame) &&
                public_cylinder.outer_step_combustion_heat_release_j >= 0.0 &&
                public_cylinder.cumulative_burned_fuel_mass_kg >= 0.0;
            if (!finite_combustion_state || !physical_combustion_state) {
                terminal_fault_ = fault(
                    finite_combustion_state ? contract::FailureKind::nonphysical_state
                                            : contract::FailureKind::numerical_failure,
                    finite_combustion_state ? "legacy-gas-nonphysical-combustion-state"
                                            : "legacy-gas-nonfinite-combustion-state",
                    finite_combustion_state
                        ? "combustion produced a physically invalid flame or "
                          "diagnostic state"
                        : "combustion produced a nonfinite flame or diagnostic "
                          "state",
                    public_cylinder.cylinder_id, std::nullopt,
                    public_cylinder.chamber_volume_id);
                return *terminal_fault_;
            }
        }
    }

    step_.indicated_gas_torque_nm = 0.0;
    for (std::size_t index = 0; index < cylinders_.size(); ++index) {
        const auto &lane = cylinders_[index];
        auto &public_cylinder = step_.cylinders[lane.public_cylinder_index];
        const auto &mechanism_cylinder = mechanics.cylinders[index];
        const auto &chamber = step_.gas_volumes[lane.chamber_volume_index].cell;
        public_cylinder.indicated_gas_torque_nm =
            (legacy_gas_pressure_pa(chamber) - ambient_pressure_pa_) *
            mechanism_cylinder.dvolume_dtheta_m3_per_rad;
        step_.indicated_gas_torque_nm += public_cylinder.indicated_gas_torque_nm;
    }
    if (!std::isfinite(step_.indicated_gas_torque_nm)) {
        terminal_fault_ = fault(contract::FailureKind::numerical_failure,
                                "legacy-gas-nonfinite-torque",
                                "post-gas indicated torque sum became nonfinite");
        return *terminal_fault_;
    }

    ++produced_sample_count_;
    return std::cref(step_);
}

bool LegacyLowOrderGasSession::faulted() const noexcept {
    return terminal_fault_.has_value();
}

std::uint64_t LegacyLowOrderGasSession::produced_sample_count() const noexcept {
    return produced_sample_count_;
}

} // namespace crankwave::simulation
