#include "bmw_m52b28_migration_test_support.hpp"

namespace engine_sim_offline::test::bmw_m52b28_migration {
namespace {

void add_restriction(ExecutionProjection &out, const std::string &path,
                     const contract::LegacyRestriction &value) {
    out.enumeration(path + ".calibration", value.calibration.value);
    out.binary64(path + ".source_rating", value.source_rating.value);
    out.binary64(path + ".resolved_k", value.resolved_k.value);
}

} // namespace

void add_mechanism(ExecutionProjection &out, const std::string &path,
                   const contract::LegacyMechanismProfile &value,
                   const ExecutionNames &names) {
    out.binary64(path + ".crank.crank_tdc_reference_rad",
                 value.crank.crank_tdc_reference_rad.value);
    out.binary64(path + ".crank.crankshaft_mass_kg",
                 value.crank.crankshaft_mass_kg.value);
    out.binary64(path + ".crank.flywheel_mass_kg",
                 value.crank.flywheel_mass_kg.value);
    out.binary64(path + ".crank.authored_crank_inertia_kg_m2",
                 value.crank.authored_crank_inertia_kg_m2.value);
    out.count(path + ".cylinders.count", value.cylinders.size());
    for (std::size_t index = 0; index < value.cylinders.size(); ++index) {
        const auto base = path + ".cylinders[" + std::to_string(index) + "]";
        const auto &cylinder = value.cylinders[index];
        out.text(base + ".topology.cylinder",
                 names.cylinder(cylinder.topology.cylinder_id));
        out.text(base + ".topology.intake_port",
                 names.port(cylinder.topology.intake_port_id));
        out.text(base + ".topology.exhaust_port",
                 names.port(cylinder.topology.exhaust_port_id));
        out.text(base + ".topology.intake_runner_volume",
                 names.volume(cylinder.topology.intake_runner_volume_id));
        out.text(base + ".topology.chamber_volume",
                 names.volume(cylinder.topology.chamber_volume_id));
        out.text(base + ".topology.exhaust_primary_volume",
                 names.volume(cylinder.topology.exhaust_primary_volume_id));
        out.text(base + ".topology.plenum_to_runner_edge",
                 names.edge(cylinder.topology.plenum_to_runner_edge_id));
        out.text(base + ".topology.intake_valve_edge",
                 names.edge(cylinder.topology.intake_valve_edge_id));
        out.text(base + ".topology.exhaust_valve_edge",
                 names.edge(cylinder.topology.exhaust_valve_edge_id));
        out.text(base + ".topology.primary_to_collector_edge",
                 names.edge(cylinder.topology.primary_to_collector_edge_id));
        out.text(base + ".topology.blowby_edge",
                 names.edge(cylinder.topology.blowby_edge_id));
        out.text(base + ".topology.exhaust_route",
                 names.route(cylinder.topology.exhaust_route_id));

        const auto &parameters = cylinder.parameters;
        out.binary64(base + ".bore_m", parameters.bore_m.value);
        out.binary64(base + ".stroke_m", parameters.stroke_m.value);
        out.binary64(base + ".crank_radius_m", parameters.crank_radius_m.value);
        out.binary64(base + ".connecting_rod_length_m",
                     parameters.connecting_rod_length_m.value);
        out.binary64(base + ".deck_height_m", parameters.deck_height_m.value);
        out.binary64(base + ".piston_compression_height_m",
                     parameters.piston_compression_height_m.value);
        out.binary64(base + ".head_chamber_volume_m3",
                     parameters.head_chamber_volume_m3.value);
        out.binary64(base + ".piston_displacement_term_m3",
                     parameters.piston_displacement_term_m3.value);
        out.binary64(base + ".piston_mass_kg", parameters.piston_mass_kg.value);
        out.binary64(base + ".connecting_rod_mass_kg",
                     parameters.connecting_rod_mass_kg.value);
        out.binary64(base + ".connecting_rod_inertia_kg_m2",
                     parameters.connecting_rod_inertia_kg_m2.value);
        out.binary64(base + ".journal_angle_rad",
                     parameters.journal_angle_rad.value);
        out.binary64(base + ".ignition_wire_angle_rad",
                     parameters.ignition_wire_angle_rad.value);
        out.binary64(base + ".header_primary_length_m",
                     parameters.header_primary_length_m.value);
    }
}

void add_gas_path(ExecutionProjection &out, const std::string &path,
                  const contract::LegacyGasPathProfile &value,
                  const ExecutionNames &names) {
    out.text(path + ".intake_topology.plenum_volume",
             names.volume(value.intake_topology.plenum_volume_id));
    out.text(path + ".intake_topology.main_throttle_edge",
             names.edge(value.intake_topology.main_throttle_edge_id));
    out.text(path + ".intake_topology.idle_bypass_edge",
             names.edge(value.intake_topology.idle_bypass_edge_id));
    out.binary64(path + ".intake.plenum_volume_m3",
                 value.intake.plenum_volume_m3.value);
    out.binary64(path + ".intake.plenum_cross_section_area_m2",
                 value.intake.plenum_cross_section_area_m2.value);
    out.binary64(path + ".intake.runner_length_m",
                 value.intake.runner_length_m.value);
    out.binary64(path + ".intake.velocity_decay",
                 value.intake.velocity_decay.value);
    out.binary64(path + ".intake.throttle_gamma",
                 value.intake.throttle_gamma.value);
    out.binary64(path + ".intake.idle_throttle_plate_position_01",
                 value.intake.idle_throttle_plate_position_01.value);
    add_restriction(out, path + ".intake.main_throttle",
                    value.intake.main_throttle);
    add_restriction(out, path + ".intake.idle_bypass",
                    value.intake.idle_bypass);
    add_restriction(out, path + ".intake.plenum_to_runner",
                    value.intake.plenum_to_runner);

    out.binary64(path + ".head.intake_runner_base_volume_m3",
                 value.head.intake_runner_base_volume_m3.value);
    out.binary64(path + ".head.intake_runner_cross_section_area_m2",
                 value.head.intake_runner_cross_section_area_m2.value);
    out.binary64(path + ".head.exhaust_runner_base_volume_m3",
                 value.head.exhaust_runner_base_volume_m3.value);
    out.binary64(path + ".head.exhaust_runner_cross_section_area_m2",
                 value.head.exhaust_runner_cross_section_area_m2.value);
    out.binary64(path + ".head.flow_table_triangle_radius_m",
                 value.head.flow_table_triangle_radius_m.value);
    const auto add_flow = [&](const std::string &name, const auto &points) {
        out.count(path + ".head." + name + ".count", points.size());
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto base = path + ".head." + name + "[" +
                              std::to_string(index) + "]";
            out.binary64(base + ".lift_m", points[index].lift_m.value);
            out.binary64(base + ".source_cfm_at_28_inh2o",
                         points[index].source_cfm_at_28_inh2o.value);
            out.binary64(base + ".resolved_k", points[index].resolved_k.value);
        }
    };
    add_flow("intake_flow", value.head.intake_flow);
    add_flow("exhaust_flow", value.head.exhaust_flow);

    out.count(path + ".exhaust_routes.count", value.exhaust_routes.size());
    for (std::size_t index = 0; index < value.exhaust_routes.size(); ++index) {
        const auto base =
            path + ".exhaust_routes[" + std::to_string(index) + "]";
        const auto &route = value.exhaust_routes[index];
        out.text(base + ".topology.route",
                 names.route(route.topology.route_id));
        out.text(base + ".topology.collector_volume",
                 names.volume(route.topology.collector_volume_id));
        out.text(base + ".topology.collector_outlet_edge",
                 names.edge(route.topology.collector_outlet_edge_id));
        out.binary64(base + ".collector_volume_m3",
                     route.parameters.collector_volume_m3.value);
        out.binary64(base + ".collector_cross_section_area_m2",
                     route.parameters.collector_cross_section_area_m2.value);
        out.binary64(base + ".exhaust_system_length_m",
                     route.parameters.exhaust_system_length_m.value);
        out.binary64(base + ".primary_tube_length_m",
                     route.parameters.primary_tube_length_m.value);
        out.binary64(base + ".velocity_decay",
                     route.parameters.velocity_decay.value);
        out.binary64(base + ".audio_volume_linear",
                     route.parameters.audio_volume_linear.value);
        add_restriction(out, base + ".primary_to_collector",
                        route.parameters.primary_to_collector);
        add_restriction(out, base + ".collector_outlet",
                        route.parameters.collector_outlet);
    }
    add_restriction(out, path + ".piston_blowby", value.piston_blowby);
}

void add_camshaft(ExecutionProjection &out, const std::string &path,
                  const contract::LegacyCamshaftProfile &value,
                  const ExecutionNames &names) {
    out.binary64(path + ".shape.maximum_lift_m",
                 value.shape.maximum_lift_m.value);
    out.binary64(path + ".shape.duration_at_reference_lift_rad",
                 value.shape.duration_at_reference_lift_rad.value);
    out.binary64(path + ".shape.exponent", value.shape.exponent.value);
    out.integer(path + ".shape.construction_steps",
                value.shape.construction_steps.value);
    out.binary64(path + ".shape.advance_rad", value.shape.advance_rad.value);
    out.binary64(path + ".shape.base_radius_m", value.shape.base_radius_m.value);
    out.count(path + ".lobes.count", value.lobes.size());
    for (std::size_t index = 0; index < value.lobes.size(); ++index) {
        const auto base = path + ".lobes[" + std::to_string(index) + "]";
        out.text(base + ".cylinder",
                 names.cylinder(value.lobes[index].cylinder_id));
        out.text(base + ".port", names.port(value.lobes[index].port_id));
        out.binary64(base + ".crank_center_rad",
                     value.lobes[index].crank_center_rad.value);
    }
}

void add_ignition_and_fuel(ExecutionProjection &out, const std::string &path,
                           const contract::LowOrderEngineCoreV1 &value,
                           const ExecutionNames &names) {
    out.count(path + ".ignition.firing_order.count",
              value.ignition.firing_order.value.size());
    for (std::size_t index = 0;
         index < value.ignition.firing_order.value.size(); ++index) {
        out.text(path + ".ignition.firing_order[" +
                     std::to_string(index) + "]",
                 names.cylinder(value.ignition.firing_order.value[index]));
    }
    out.binary64(path + ".ignition.timing_curve_triangle_radius_rad_s",
                 value.ignition.timing_curve_triangle_radius_rad_s.value);
    out.count(path + ".ignition.timing_curve.count",
              value.ignition.timing_curve.size());
    for (std::size_t index = 0; index < value.ignition.timing_curve.size();
         ++index) {
        const auto base =
            path + ".ignition.timing_curve[" + std::to_string(index) + "]";
        out.binary64(base + ".angular_speed_rad_s",
                     value.ignition.timing_curve[index].angular_speed_rad_s.value);
        out.binary64(base + ".timing_advance_rad",
                     value.ignition.timing_curve[index].timing_advance_rad.value);
    }
    out.binary64(path + ".ignition.limiter_speed_rpm",
                 value.ignition.limiter_speed_rpm.value);
    out.binary64(path + ".ignition.limiter_hold_s",
                 value.ignition.limiter_hold_s.value);
    out.binary64(path + ".ignition.declared_redline_rpm",
                 value.ignition.declared_redline_rpm.value);

    // Product fuel IDs differ intentionally; every executable fuel scalar remains
    // exact and is compared here.
    const auto &fuel = value.fuel;
    out.binary64(path + ".fuel.molecular_mass_kg_per_mol",
                 fuel.molecular_mass_kg_per_mol.value);
    out.binary64(path + ".fuel.energy_density_j_per_kg",
                 fuel.energy_density_j_per_kg.value);
    out.binary64(path + ".fuel.molecular_afr", fuel.molecular_afr.value);
    out.binary64(path + ".fuel.maximum_burning_efficiency_01",
                 fuel.maximum_burning_efficiency_01.value);
    out.binary64(path + ".fuel.burning_efficiency_randomness_01",
                 fuel.burning_efficiency_randomness_01.value);
    out.binary64(path + ".fuel.low_efficiency_attenuation_01",
                 fuel.low_efficiency_attenuation_01.value);
    out.binary64(path + ".fuel.maximum_turbulence_effect",
                 fuel.maximum_turbulence_effect.value);
    out.binary64(path + ".fuel.maximum_dilution_effect",
                 fuel.maximum_dilution_effect.value);
    out.binary64(path + ".fuel.lbv_multiplier", fuel.lbv_multiplier.value);
    out.binary64(path + ".fuel.turbulence_to_flame_speed_ratio_triangle_radius",
                 fuel.turbulence_to_flame_speed_ratio_triangle_radius.value);
    out.count(path + ".fuel.turbulence_to_flame_speed_ratio.count",
              fuel.turbulence_to_flame_speed_ratio.size());
    for (std::size_t index = 0;
         index < fuel.turbulence_to_flame_speed_ratio.size(); ++index) {
        const auto base = path + ".fuel.turbulence_to_flame_speed_ratio[" +
                          std::to_string(index) + "]";
        out.binary64(base + ".turbulence",
                     fuel.turbulence_to_flame_speed_ratio[index].turbulence.value);
        out.binary64(
            base + ".flame_speed_ratio",
            fuel.turbulence_to_flame_speed_ratio[index].flame_speed_ratio.value);
    }
}

void add_excitation(ExecutionProjection &out, const std::string &path,
                    const contract::LegacyReferenceExcitationProfile &value,
                    const ExecutionNames &names) {
    out.binary64(path + ".reference_atmosphere_pa_abs",
                 value.reference_atmosphere_pa_abs.value);
    out.binary64(path + ".legacy_propagation_speed_m_s",
                 value.legacy_propagation_speed_m_s.value);
    out.binary64(path + ".excitation_scale", value.excitation_scale.value);
    out.binary64(path + ".filtered_speed_threshold_rpm",
                 value.filtered_speed_threshold_rpm.value);
    out.integer(path + ".filtered_speed_exponent",
                value.filtered_speed_exponent.value);
    out.binary64(path + ".pressure_gains.gauge_static",
                 value.pressure_gains.gauge_static.value);
    out.binary64(path + ".pressure_gains.dynamic_forward",
                 value.pressure_gains.dynamic_forward.value);
    out.binary64(path + ".pressure_gains.dynamic_reverse",
                 value.pressure_gains.dynamic_reverse.value);
    out.binary64(path + ".cylinder_count_divisor",
                 value.cylinder_count_divisor.value);
    out.binary64(path + ".inverse_length_exponent",
                 value.inverse_length_exponent.value);
    out.rate(path + ".delay_rate", value.delay_rate.value);
    out.count(path + ".cylinder_accumulation_order.count",
              value.cylinder_accumulation_order.value.size());
    for (std::size_t index = 0;
         index < value.cylinder_accumulation_order.value.size(); ++index) {
        out.text(path + ".cylinder_accumulation_order[" +
                     std::to_string(index) + "]",
                 names.cylinder(value.cylinder_accumulation_order.value[index]));
    }
    out.count(path + ".cylinder_paths.count", value.cylinder_paths.size());
    for (std::size_t index = 0; index < value.cylinder_paths.size(); ++index) {
        const auto base =
            path + ".cylinder_paths[" + std::to_string(index) + "]";
        const auto &cylinder = value.cylinder_paths[index];
        out.text(base + ".cylinder", names.cylinder(cylinder.cylinder_id));
        out.text(base + ".route", names.route(cylinder.route_id));
        out.binary64(base + ".header_primary_length_m",
                     cylinder.header_primary_length_m.value);
        out.binary64(base + ".sound_attenuation_linear",
                     cylinder.sound_attenuation_linear.value);
        out.integer(base + ".resolved_delay_samples",
                    cylinder.resolved_delay_samples.value);
    }
    out.count(path + ".routes.count", value.routes.size());
    for (std::size_t index = 0; index < value.routes.size(); ++index) {
        const auto base = path + ".routes[" + std::to_string(index) + "]";
        out.text(base + ".route", names.route(value.routes[index].route_id));
        out.binary64(base + ".exhaust_system_length_m",
                     value.routes[index].exhaust_system_length_m.value);
        out.binary64(base + ".audio_volume_linear",
                     value.routes[index].audio_volume_linear.value);
    }
}

void add_torque_capability(ExecutionProjection &out, const std::string &path,
                           const contract::TorqueCapability &value) {
    const auto add_form = [&](const std::string &base,
                              const contract::NetTorqueFormCapability &form) {
        out.enumeration(base + ".availability", form.availability);
        out.enumeration(base + ".completeness", form.completeness);
        out.integer(base + ".included_terms", form.included_terms);
        out.integer(base + ".omitted_terms", form.omitted_terms);
    };
    add_form(path + ".instantaneous_net_shaft", value.instantaneous_net_shaft);
    add_form(path + ".cycle_mean_net_shaft", value.cycle_mean_net_shaft);
    out.boolean(path + ".equivalent_inertia_available",
                value.equivalent_inertia_available);
}

} // namespace engine_sim_offline::test::bmw_m52b28_migration
