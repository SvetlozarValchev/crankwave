#include "bake_vehicleengine_command_support.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace {

using engine_sim_offline::cli::BakeVehicleEngineError;
using engine_sim_offline::cli::BakeVehicleEngineErrorKind;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] BakeVehicleEngineError failure(std::string code) {
    BakeVehicleEngineError result;
    result.kind = BakeVehicleEngineErrorKind::software;
    result.code = std::move(code);
    result.stage = "worker-order-test";
    result.message = "planned worker failure";
    return result;
}

void test_native_output_failure_classification() {
    using engine_sim_offline::cli::NativeOutputErrorKind;
    using engine_sim_offline::cli::detail::bake_output_error_kind;
    expect(bake_output_error_kind(NativeOutputErrorKind::cant_create) ==
               BakeVehicleEngineErrorKind::cant_create,
           "permanent output failure lost cant-create classification");
    expect(bake_output_error_kind(NativeOutputErrorKind::temp_fail) ==
               BakeVehicleEngineErrorKind::temporary_failure,
           "temporary output failure was not preserved for exit 75");
    expect(engine_sim_offline::cli::bake_vehicleengine_error_kind_label(
               BakeVehicleEngineErrorKind::temporary_failure) == "temporary-failure",
           "temporary output failure lost its stable label");
}

void test_lowest_ordinal_failure_is_schedule_independent() {
    const std::array inputs{0, 1, 2};
    std::atomic<bool> higher_ordinal_failed{false};
    std::atomic<bool> worker_token_was_stopped{false};
    const auto result =
        engine_sim_offline::cli::detail::parallel_map_ordered_with_workers<int>(
            std::span<const int>{inputs}, "worker-order-test", {}, 3U,
            [&](const int ordinal, const std::stop_token stop_token)
                -> std::variant<int, BakeVehicleEngineError> {
                if (ordinal == 2) {
                    higher_ordinal_failed.store(true, std::memory_order_release);
                    return failure("higher-ordinal-failure");
                }
                while (!higher_ordinal_failed.load(std::memory_order_acquire)) {
                    if (stop_token.stop_requested()) {
                        worker_token_was_stopped.store(true, std::memory_order_relaxed);
                        return failure("internally-cancelled-lower-ordinal");
                    }
                    std::this_thread::yield();
                }
                if (ordinal == 0) {
                    return failure("lowest-ordinal-failure");
                }
                return ordinal;
            });
    const auto *reported = std::get_if<BakeVehicleEngineError>(&result);
    expect(reported != nullptr && reported->code == "lowest-ordinal-failure",
           "worker scheduling changed the reported concrete failure");
    expect(!worker_token_was_stopped.load(std::memory_order_relaxed),
           "a worker failure incorrectly propagated as cancellation");
}

void test_success_results_retain_input_order() {
    const std::array inputs{3, 1, 2, 0};
    const auto result =
        engine_sim_offline::cli::detail::parallel_map_ordered_with_workers<int>(
            std::span<const int>{inputs}, "worker-order-test", {}, 4U,
            [](const int value,
               const std::stop_token) -> std::variant<int, BakeVehicleEngineError> {
                for (int count = 0; count < value * 16; ++count) {
                    std::this_thread::yield();
                }
                return value * 10;
            });
    const auto *outputs = std::get_if<std::vector<int>>(&result);
    expect(outputs != nullptr && *outputs == std::vector<int>({30, 10, 20, 0}),
           "parallel worker completion order leaked into canonical output order");
}

} // namespace

int main() {
    try {
        test_native_output_failure_classification();
        test_lowest_ordinal_failure_is_schedule_independent();
        test_success_results_retain_input_order();
    } catch (const std::exception &error) {
        std::cerr << "bake VEHICLEENGINE command test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
