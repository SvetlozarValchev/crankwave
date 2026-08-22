#pragma once

#include "bake_crankwave_command.hpp"
#include "native_input_files.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::cli::detail {

[[nodiscard]] constexpr BakeCrankwaveErrorKind
bake_output_error_kind(const NativeOutputErrorKind kind) noexcept {
    switch (kind) {
    case NativeOutputErrorKind::cant_create:
        return BakeCrankwaveErrorKind::cant_create;
    case NativeOutputErrorKind::temp_fail:
        return BakeCrankwaveErrorKind::temporary_failure;
    }
    return BakeCrankwaveErrorKind::software;
}

[[nodiscard]] inline BakeCrankwaveError
parallel_bake_error(const BakeCrankwaveErrorKind kind, std::string code,
                    const std::string_view stage, std::string message) {
    BakeCrankwaveError result;
    result.kind = kind;
    result.code = std::move(code);
    result.stage = std::string{stage};
    result.message = std::move(message);
    return result;
}

// Internal/testable worker-pool seam. Inputs are claimed in ordinal order and
// outputs are always returned in that order. A task failure prevents unclaimed
// later ordinals from starting, but never cancels already-claimed lower ordinals;
// this makes the reported lowest-ordinal concrete failure scheduling-independent.
template <class Output, class Input, class Operation>
[[nodiscard]] std::variant<std::vector<Output>, BakeCrankwaveError>
parallel_map_ordered_with_workers(const std::span<const Input> inputs,
                                  const std::string_view stage,
                                  const std::stop_token parent_stop,
                                  const std::size_t requested_worker_count,
                                  Operation operation) {
    if (parent_stop.stop_requested()) {
        return parallel_bake_error(BakeCrankwaveErrorKind::cancelled,
                                   "bake-crankwave-cancelled", stage,
                                   "native responsive bake was cancelled");
    }
    if (inputs.empty()) {
        return parallel_bake_error(BakeCrankwaveErrorKind::software,
                                   "native-responsive-parallel-plan-empty", stage,
                                   "parallel responsive capture plan is empty");
    }
    const auto worker_count =
        std::min(std::max<std::size_t>(requested_worker_count, 1U), inputs.size());

    std::stop_source parent_cancellation;
    std::stop_callback parent_callback{parent_stop, [&parent_cancellation] {
                                           static_cast<void>(
                                               parent_cancellation.request_stop());
                                       }};
    std::atomic<std::size_t> next{0U};
    std::atomic<std::size_t> lowest_failed_ordinal{inputs.size()};
    std::vector<std::optional<Output>> outputs(inputs.size());
    std::vector<std::optional<BakeCrankwaveError>> failures(inputs.size());
    std::optional<BakeCrankwaveError> setup_failure;
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);

    const auto record_failure = [&](const std::size_t index,
                                    BakeCrankwaveError failure) {
        failures[index] = std::move(failure);
        auto observed = lowest_failed_ordinal.load(std::memory_order_relaxed);
        while (index < observed && !lowest_failed_ordinal.compare_exchange_weak(
                                       observed, index, std::memory_order_release,
                                       std::memory_order_relaxed)) {
        }
    };
    const auto worker = [&] {
        for (;;) {
            if (parent_cancellation.stop_requested()) {
                return;
            }

            auto index = next.load(std::memory_order_relaxed);
            for (;;) {
                const auto lowest_failure =
                    lowest_failed_ordinal.load(std::memory_order_acquire);
                if (index >= inputs.size() || index > lowest_failure) {
                    return;
                }
                if (next.compare_exchange_weak(index, index + 1U,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed)) {
                    break;
                }
                if (parent_cancellation.stop_requested()) {
                    return;
                }
            }

            try {
                auto result = operation(inputs[index], parent_cancellation.get_token());
                if (auto *failure = std::get_if<BakeCrankwaveError>(&result)) {
                    record_failure(index, std::move(*failure));
                    return;
                }
                outputs[index].emplace(std::get<Output>(std::move(result)));
            } catch (const std::bad_alloc &) {
                record_failure(index,
                               parallel_bake_error(
                                   BakeCrankwaveErrorKind::software,
                                   "native-responsive-worker-memory-exhausted", stage,
                                   "responsive capture worker exhausted memory"));
                return;
            } catch (const std::exception &failure) {
                record_failure(index,
                               parallel_bake_error(
                                   BakeCrankwaveErrorKind::software,
                                   "native-responsive-worker-failed", stage,
                                   std::string{"responsive capture worker failed: "} +
                                       failure.what()));
                return;
            } catch (...) {
                record_failure(index,
                               parallel_bake_error(
                                   BakeCrankwaveErrorKind::software,
                                   "native-responsive-worker-failed", stage,
                                   "responsive capture worker failed unexpectedly"));
                return;
            }
        }
    };

    try {
        for (std::size_t index = 0U; index < worker_count; ++index) {
            workers.emplace_back(worker);
        }
    } catch (const std::exception &failure) {
        setup_failure = parallel_bake_error(
            BakeCrankwaveErrorKind::software, "native-responsive-worker-start-failed",
            stage,
            std::string{"could not start responsive capture workers: "} +
                failure.what());
        static_cast<void>(parent_cancellation.request_stop());
    } catch (...) {
        setup_failure = parallel_bake_error(
            BakeCrankwaveErrorKind::software, "native-responsive-worker-start-failed",
            stage, "could not start responsive capture workers");
        static_cast<void>(parent_cancellation.request_stop());
    }
    for (auto &thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    if (setup_failure.has_value()) {
        return std::move(*setup_failure);
    }

    // A concrete failure is more informative than parent cancellation. Among
    // concrete failures, ordinal order is the stable plan authority.
    for (auto &failure : failures) {
        if (failure.has_value() && failure->kind != BakeCrankwaveErrorKind::cancelled) {
            return std::move(*failure);
        }
    }
    if (parent_stop.stop_requested()) {
        return parallel_bake_error(BakeCrankwaveErrorKind::cancelled,
                                   "bake-crankwave-cancelled", stage,
                                   "native responsive bake was cancelled");
    }
    for (auto &failure : failures) {
        if (failure.has_value()) {
            return std::move(*failure);
        }
    }

    std::vector<Output> result;
    result.reserve(outputs.size());
    for (auto &output : outputs) {
        if (!output.has_value()) {
            return parallel_bake_error(
                BakeCrankwaveErrorKind::software,
                "native-responsive-worker-result-missing", stage,
                "responsive worker pool drained without every result");
        }
        result.push_back(std::move(*output));
    }
    return result;
}

} // namespace crankwave::cli::detail
