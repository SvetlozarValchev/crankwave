#include "package/package_source_capture_set.hpp"

#include "determinism/renderer_numeric_environment.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::package_detail {
namespace {

enum class WorkerSlotFailure : std::uint8_t {
    none,
    allocation,
    numeric_environment,
    unexpected_exception,
};

struct WorkerSlot {
    std::optional<PackageSourceCaptureResult> result;
    WorkerSlotFailure failure = WorkerSlotFailure::none;
};

[[nodiscard]] PackageSourceCaptureError
coordinator_source_error(const PackageSourceCaptureErrorCode code,
                         std::string detail_code, std::string message) {
    return {
        code,
        std::move(detail_code),
        std::move(message),
        std::nullopt,
    };
}

[[nodiscard]] PackageSourceCaptureSetError
coordinator_error(const PackageSourceCaptureSetErrorCode code,
                  const std::size_t source_index, std::string source_id,
                  PackageSourceCaptureError source_error) {
    return {
        code,
        source_index,
        std::move(source_id),
        std::move(source_error),
    };
}

[[nodiscard]] PackageSourceCaptureSetError
source_slot_error(const std::size_t source_index,
                  const CompiledPackageBakeScenarioSource &source,
                  PackageSourceCaptureError source_error) {
    return coordinator_error(PackageSourceCaptureSetErrorCode::source_capture_failed,
                             source_index, source.id, std::move(source_error));
}

[[nodiscard]] PackageSourceCaptureSetError
worker_slot_error(const std::size_t source_index,
                  const CompiledPackageBakeScenarioSource &source,
                  const WorkerSlotFailure failure) {
    if (failure == WorkerSlotFailure::allocation) {
        return coordinator_error(
            PackageSourceCaptureSetErrorCode::resource_limit, source_index, source.id,
            coordinator_source_error(
                PackageSourceCaptureErrorCode::incomplete_session,
                "package-source-capture-set-worker-allocation-failed",
                "allocation failed while capturing an authored package source"));
    }
    if (failure == WorkerSlotFailure::numeric_environment) {
        return coordinator_error(
            PackageSourceCaptureSetErrorCode::worker_numeric_environment_rejected,
            source_index, source.id,
            coordinator_source_error(
                PackageSourceCaptureErrorCode::session_failed,
                "package-source-capture-set-worker-numeric-environment-rejected",
                "a package source worker did not satisfy the renderer numeric "
                "environment"));
    }
    return coordinator_error(
        PackageSourceCaptureSetErrorCode::worker_execution_failed, source_index,
        source.id,
        coordinator_source_error(
            PackageSourceCaptureErrorCode::session_failed,
            "package-source-capture-set-worker-exception",
            "an unexpected exception escaped authored package source capture"));
}

[[nodiscard]] std::string
first_source_id(const std::span<const CompiledPackageBakeScenarioSource> sources) {
    return sources.empty() ? std::string{} : sources.front().id;
}

} // namespace

PackageSourceCaptureSetResult
capture_package_source_set(const CompiledPackageBake &plan,
                           const std::size_t worker_limit) noexcept {
    const auto sources = plan.scenario_sources();
    if (worker_limit == 0U) {
        return coordinator_error(
            PackageSourceCaptureSetErrorCode::invalid_worker_limit, 0U,
            first_source_id(sources),
            coordinator_source_error(
                PackageSourceCaptureErrorCode::invalid_bus_selection,
                "package-source-capture-set-zero-worker-limit",
                "package source capture requires a positive worker limit"));
    }
    if (sources.empty()) {
        return coordinator_error(
            PackageSourceCaptureSetErrorCode::invalid_plan, 0U, {},
            coordinator_source_error(
                PackageSourceCaptureErrorCode::incomplete_session,
                "package-source-capture-set-no-sources",
                "compiled package-bake plan contains no authored sources"));
    }

    try {
        std::vector<std::string_view> selected_bus_ids;
        selected_bus_ids.reserve(plan.audio_buses().size());
        for (const auto &bus : plan.audio_buses()) {
            selected_bus_ids.push_back(bus.session_bus_id);
        }

        std::vector<WorkerSlot> slots(sources.size());
        const auto worker_count = std::min(worker_limit, sources.size());
        std::optional<std::size_t> worker_start_failure;
        bool worker_start_allocation_failure = false;

        {
            std::vector<std::jthread> workers;
            workers.reserve(worker_count);
            for (std::size_t worker_index = 0U; worker_index < worker_count;
                 ++worker_index) {
                try {
                    workers.emplace_back([&, worker_index] {
                        if (std::holds_alternative<
                                determinism::RendererNumericEnvironmentError>(
                                determinism::renderer_numeric_environment())) {
                            slots[worker_index].failure =
                                WorkerSlotFailure::numeric_environment;
                            return;
                        }
                        for (std::size_t source_index = worker_index;
                             source_index < sources.size();
                             source_index += worker_count) {
                            try {
                                slots[source_index].result.emplace(
                                    capture_package_source_lane(
                                        sources[source_index].scenario,
                                        selected_bus_ids));
                            } catch (const std::bad_alloc &) {
                                slots[source_index].failure =
                                    WorkerSlotFailure::allocation;
                            } catch (...) {
                                slots[source_index].failure =
                                    WorkerSlotFailure::unexpected_exception;
                            }
                        }
                    });
                } catch (const std::bad_alloc &) {
                    worker_start_failure = worker_index;
                    worker_start_allocation_failure = true;
                    break;
                } catch (const std::system_error &) {
                    worker_start_failure = worker_index;
                    break;
                } catch (...) {
                    worker_start_failure = worker_index;
                    break;
                }
            }
            // std::jthread destruction joins every worker that was successfully
            // started before any result or failure slot is inspected.
        }

        if (worker_start_failure.has_value()) {
            auto &slot = slots[*worker_start_failure];
            if (!slot.result.has_value() && slot.failure == WorkerSlotFailure::none) {
                slot.failure = worker_start_allocation_failure
                                   ? WorkerSlotFailure::allocation
                                   : WorkerSlotFailure::unexpected_exception;
            }
        }

        // Authored-order scanning makes the selected failure independent of
        // thread completion order. All successful captures are discarded when
        // any slot failed.
        for (std::size_t index = 0U; index < slots.size(); ++index) {
            auto &slot = slots[index];
            if (slot.failure != WorkerSlotFailure::none) {
                if (worker_start_failure == index) {
                    if (worker_start_allocation_failure) {
                        return coordinator_error(
                            PackageSourceCaptureSetErrorCode::resource_limit, index,
                            sources[index].id,
                            coordinator_source_error(
                                PackageSourceCaptureErrorCode::incomplete_session,
                                "package-source-capture-set-worker-start-allocation-"
                                "failed",
                                "allocation failed while starting a bounded package "
                                "source capture worker"));
                    }
                    return coordinator_error(
                        PackageSourceCaptureSetErrorCode::worker_start_failed, index,
                        sources[index].id,
                        coordinator_source_error(
                            PackageSourceCaptureErrorCode::session_failed,
                            "package-source-capture-set-worker-start-failed",
                            "a bounded package source capture worker could not be "
                            "started"));
                }
                return worker_slot_error(index, sources[index], slot.failure);
            }
            if (!slot.result.has_value()) {
                return coordinator_error(
                    PackageSourceCaptureSetErrorCode::worker_execution_failed, index,
                    sources[index].id,
                    coordinator_source_error(
                        PackageSourceCaptureErrorCode::incomplete_session,
                        "package-source-capture-set-source-not-run",
                        "an authored package source was not captured by a worker"));
            }
            if (auto *error = std::get_if<PackageSourceCaptureError>(&*slot.result)) {
                return source_slot_error(index, sources[index], std::move(*error));
            }
        }

        PackageSourceCaptureSet capture_set;
        capture_set.sources.reserve(sources.size());
        for (auto &slot : slots) {
            capture_set.sources.push_back(
                std::get<PackageSourceLaneCapture>(std::move(*slot.result)));
        }
        return capture_set;
    } catch (const std::bad_alloc &) {
        return coordinator_error(
            PackageSourceCaptureSetErrorCode::resource_limit, 0U,
            first_source_id(sources),
            coordinator_source_error(PackageSourceCaptureErrorCode::incomplete_session,
                                     "package-source-capture-set-allocation-failed",
                                     "allocation failed while preparing or collecting "
                                     "package source captures"));
    } catch (...) {
        return coordinator_error(
            PackageSourceCaptureSetErrorCode::worker_execution_failed, 0U,
            first_source_id(sources),
            coordinator_source_error(PackageSourceCaptureErrorCode::session_failed,
                                     "package-source-capture-set-unexpected-exception",
                                     "an unexpected exception interrupted package "
                                     "source capture coordination"));
    }
}

} // namespace engine_sim_offline::package_detail
