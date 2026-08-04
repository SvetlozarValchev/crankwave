#pragma once

#include "engine_sim_offline/package_bake.hpp"
#include "package/package_source_capture.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace engine_sim_offline::package_detail {

struct PackageSourceCaptureSet {
    // Captures retain the exact authored order of
    // CompiledPackageBake::scenario_sources().
    std::vector<PackageSourceLaneCapture> sources;
};

enum class PackageSourceCaptureSetErrorCode : std::uint8_t {
    invalid_worker_limit,
    invalid_plan,
    source_capture_failed,
    resource_limit,
    worker_start_failed,
    worker_numeric_environment_rejected,
    worker_execution_failed,
};

struct PackageSourceCaptureSetError {
    PackageSourceCaptureSetErrorCode code =
        PackageSourceCaptureSetErrorCode::source_capture_failed;
    std::size_t source_index = 0U;
    std::string source_id;
    // A source failure is retained verbatim. Coordinator failures use the same
    // nested shape with a coordinator-specific detail code so callers have one
    // stable diagnostic path for every failed source slot.
    PackageSourceCaptureError source_error;
};

using PackageSourceCaptureSetResult =
    std::variant<PackageSourceCaptureSet, PackageSourceCaptureSetError>;

// Captures every authored source exactly once using at most worker_limit worker
// threads. Each worker owns only independent finite EngineSession instances;
// scheduling cannot affect returned source order or failure selection.
//
// Every worker admits its own thread-local renderer numeric environment before
// creating a session. A rejected worker produces no source capture.
[[nodiscard]] PackageSourceCaptureSetResult
capture_package_source_set(const CompiledPackageBake &plan,
                           std::size_t worker_limit = 4U) noexcept;

} // namespace engine_sim_offline::package_detail
