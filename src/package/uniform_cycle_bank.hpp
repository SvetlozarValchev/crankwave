#pragma once

#include "engine_sim_offline/contract/audio_package.hpp"
#include "engine_sim_offline/package_bake.hpp"
#include "engine_sim_offline/session.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::package_detail {

// The evidence may be owned by a capture or borrowed from any stable contiguous
// store. Boundary coordinates remain on the session-global delivery clock here.
struct UniformCycleLaneView {
    std::span<const EngineCompletedCycleEvidence> cycles;
    std::uint64_t first_global_delivery_frame = 0U;
    std::uint64_t pcm_frame_count = 0U;
};

struct UniformRunningCycleBankRequest {
    UniformCycleLaneView lane;
    PackageBakeMethodGeometry geometry;
    double padded_minimum_rpm = 0.0;
    double padded_maximum_rpm = 0.0;
    double load_coordinate = 0.0;
    authoring::PackageBakeRunningDirection direction =
        authoring::PackageBakeRunningDirection::rising;
};

struct UniformIdleCyclePoolRequest {
    UniformCycleLaneView lane;
    PackageBakeMethodGeometry geometry;
    double playback_idle_rpm = 0.0;
    double load_coordinate = 0.0;
};

struct UniformCycleBank {
    std::vector<contract::AudioPackageCycleUnit> units;
    contract::AudioPackageLoadCalibration load_calibration;
    double total_squared_rpm_error = 0.0;
    std::uint64_t rejected_cycle_count = 0U;
};

enum class UniformCycleBankErrorCode : std::uint8_t {
    invalid_request,
    malformed_evidence,
    cycle_rejected,
    impossible_coverage,
    resource_limit,
    internal_failure,
};

struct UniformCycleBankError {
    UniformCycleBankErrorCode code = UniformCycleBankErrorCode::invalid_request;
    std::string path;
    std::string detail;
};

using UniformCycleBankResult = std::variant<UniformCycleBank, UniformCycleBankError>;

[[nodiscard]] UniformCycleBankResult assign_uniform_running_cycle_bank(
    const UniformRunningCycleBankRequest &request) noexcept;

[[nodiscard]] UniformCycleBankResult
retain_uniform_idle_cycle_pool(const UniformIdleCyclePoolRequest &request) noexcept;

} // namespace engine_sim_offline::package_detail
