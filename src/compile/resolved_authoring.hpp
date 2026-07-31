#pragma once

#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine_sim_offline::compile::detail {

// Engine-owned scenario inputs retained after authoring resolution. Keeping these
// descriptors beside both resolvers avoids a lossy translation layer and keeps the
// public JSON identity separate from internal route IDs.
struct ResolvedFuelDescriptor {
    std::string authored_id;
    contract::ResolvedValue<std::string> fuel_id;
    contract::ResolvedValue<double> lower_heating_value_j_per_kg;
    contract::ResolvedValue<double> molecular_mass_kg_per_mol;
    contract::ResolvedValue<double> molecular_air_fuel_ratio;

    friend bool operator==(const ResolvedFuelDescriptor &,
                           const ResolvedFuelDescriptor &) = default;
};

struct ResolvedAudioBusDescriptor {
    std::string authored_id;
    std::string semantic_id;
    contract::OutputBusKind kind = contract::OutputBusKind::unspecified;
    // Order is the authored deterministic arithmetic reduction order.
    std::vector<contract::RouteId> routes;
    double gain_linear = 1.0;
    contract::AudioSampleEncoding sample_encoding =
        contract::AudioSampleEncoding::float32le;
    bool selectable = true;
    bool diagnostic = false;

    friend bool operator==(const ResolvedAudioBusDescriptor &,
                           const ResolvedAudioBusDescriptor &) = default;
};

struct ResolvedVehicleDescriptor {
    std::uint32_t runtime_id = 0;
    contract::ResolvedValue<std::string> semantic_id;
    contract::ResolvedValue<double> mass_kg;
    contract::ResolvedValue<double> drag_coefficient;
    contract::ResolvedValue<double> frontal_area_m2;
    contract::ResolvedValue<double> differential_ratio;
    contract::ResolvedValue<double> tire_radius_m;
    contract::ResolvedValue<double> rolling_resistance_force_n;
    std::optional<contract::ResolvedValue<double>> maximum_service_brake_force_n;

    friend bool operator==(const ResolvedVehicleDescriptor &,
                           const ResolvedVehicleDescriptor &) = default;
};

struct ResolvedGearDescriptor {
    std::uint32_t runtime_id = 0;
    // One-based authored position. The vector and this ordinal are deliberately not
    // sorted by stable ID: transmission gear order is executable product data.
    contract::ResolvedValue<std::uint32_t> authored_ordinal;
    contract::ResolvedValue<std::string> semantic_id;
    contract::ResolvedValue<double> ratio;

    friend bool operator==(const ResolvedGearDescriptor &,
                           const ResolvedGearDescriptor &) = default;
};

struct ResolvedTransmissionDescriptor {
    std::uint32_t runtime_id = 0;
    contract::ResolvedValue<std::string> semantic_id;
    contract::ResolvedValue<double> maximum_clutch_torque_nm;
    std::vector<ResolvedGearDescriptor> gears;

    friend bool operator==(const ResolvedTransmissionDescriptor &,
                           const ResolvedTransmissionDescriptor &) = default;
};

struct ResolvedDynoDefaultsDescriptor {
    contract::ResolvedValue<double> minimum_engine_speed_rad_s;
    contract::ResolvedValue<double> maximum_engine_speed_rad_s;
    contract::ResolvedValue<double> hold_step_rad_s;

    friend bool operator==(const ResolvedDynoDefaultsDescriptor &,
                           const ResolvedDynoDefaultsDescriptor &) = default;
};

// Engine-package-owned operating equipment. This is intentionally not part of
// EngineSpec: an engine can be mounted to different vehicles or test-cell rigs
// without changing its physical engine identity.
struct ResolvedRigDescriptor {
    std::uint32_t runtime_id = 0;
    contract::ResolvedValue<std::string> semantic_id;
    std::optional<ResolvedVehicleDescriptor> vehicle;
    std::optional<ResolvedTransmissionDescriptor> transmission;
    std::optional<ResolvedDynoDefaultsDescriptor> dyno_defaults;

    friend bool operator==(const ResolvedRigDescriptor &,
                           const ResolvedRigDescriptor &) = default;
};

} // namespace engine_sim_offline::compile::detail
