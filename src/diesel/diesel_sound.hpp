#pragma once

#include "crankwave/responsive/native_package.hpp"
#include "simulation/diesel_cylinder_v1.hpp"

#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace crankwave::diesel {

inline constexpr std::string_view kMechanicalDieselModel = "mechanical-diesel-v1";
inline constexpr std::string_view kMechanicalDieselRuntime =
    "diesel-mechanical-audio-v1";

struct SoundError {
    std::string path;
    std::string message;
    bool cancelled = false;
};

// Optional steady boundary map, in RPM-major / fueling-minor bake order.
// These are prescribed reservoirs, not a solved turbocharger or ECU.
struct SoundBoundaryCell {
    simulation::DieselGasBoundary intake;
    simulation::DieselGasBoundary exhaust;
};

// The initial family has identical cylinder hardware. Each binding retains its
// stable ID, firing phase, fuel trim, and structural coupling during playback.
struct SoundDefinition {
    std::string engine_id;
    std::uint32_t runtime_version = 1;
    simulation::DieselCylinderParameters cylinder;
    std::vector<simulation::DieselCrankInjectionPulse> pulses;
    std::vector<double> rpms;
    std::vector<double> fueling;
    std::vector<SoundBoundaryCell> operating_boundaries;
    std::uint32_t samples_per_cycle = 0;
    std::uint32_t minimum_settling_cycles = 0;
    std::uint32_t maximum_settling_cycles = 0;
    double settling_relative_tolerance = 0.0;
    std::string cylinders_json;
    std::string presentation_json;
    std::string provenance_json;
};

struct BakedSound {
    std::vector<responsive::PortableResponsivePackageMember> members;
    std::size_t cell_count = 0;
};

[[nodiscard]] std::variant<SoundDefinition, SoundError>
parse_sound_definition(std::string_view json);

// Captures settled pressure-force and outward exhaust-flow sources on one
// canonical 720-degree phase grid. Structural filtering is deliberately downstream.
[[nodiscard]] std::variant<BakedSound, SoundError>
bake_sound(std::string_view source_json, std::string_view backend_identity_json,
           std::stop_token stop = {});

} // namespace crankwave::diesel
