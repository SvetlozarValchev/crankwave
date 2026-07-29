#pragma once

#include "engine_sim_offline/contract/result.hpp"
#include "experimental/physical_exhaust_network.hpp"

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::experimental {

enum class BmwPhysicalListeningMode {
    held_3000_throttle_0p85,
    inertial_dyno,
};

struct BmwPhysicalInputHistory {
    std::string scenario_id;
    double ambient_pressure_pa = 0.0;
    double ambient_temperature_k = 0.0;
    double audible_start_s = 0.0;
    double audible_duration_s = 0.0;
    double total_duration_s = 0.0;
    std::vector<std::array<PhysicalValveBoundary, 6>> frames_10khz;
    std::optional<contract::HeldSpeedOperatingPointResult> held;
    std::optional<contract::InertialDynoResult> inertial;
};

using BmwPhysicalInputCaptureResult =
    std::variant<BmwPhysicalInputHistory, std::string>;

[[nodiscard]] BmwPhysicalInputCaptureResult
capture_bmw_physical_input(BmwPhysicalListeningMode mode);

} // namespace engine_sim_offline::experimental
