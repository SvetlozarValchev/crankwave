#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace engine_sim_offline::authoring {

// Product identities remain stable strings. Dense numeric execution IDs are assigned
// only by the compiler and never appear in authored documents.
template <class Tag> struct StableId {
    std::string value;

    friend bool operator==(const StableId &, const StableId &) = default;
};

template <class Tag> struct StableRef {
    std::string value;

    friend bool operator==(const StableRef &, const StableRef &) = default;
};

enum class QuantityDimension : std::uint8_t {
    dimensionless,
    angle,
    angular_speed,
    area,
    density,
    duration,
    energy_per_mass,
    force,
    frequency,
    length,
    mass,
    mass_flow_rate,
    molar_mass,
    moment_of_inertia,
    power,
    pressure,
    pressure_per_speed,
    pressure_per_speed_squared,
    speed,
    temperature,
    torque,
    volume,
    volume_flow_rate,
};

// Every dimensional authored number uses this representation. `standard` preserves a
// calibration basis which a unit alone cannot express, such as a flow-bench pressure
// convention.
struct Quantity {
    double value = 0.0;
    std::string unit;
    std::optional<std::string> standard;

    friend bool operator==(const Quantity &, const Quantity &) = default;
};

// Exact rates do not pass through binary64 during authoring or compilation.
struct RationalRate {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 1;
    std::string unit = "Hz";

    friend bool operator==(const RationalRate &, const RationalRate &) = default;
};

} // namespace engine_sim_offline::authoring
