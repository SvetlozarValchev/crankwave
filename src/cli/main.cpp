#include "cli_app.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stop_token>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::atomic_flag termination_requested = ATOMIC_FLAG_INIT;

extern "C" void observe_termination_signal(int) noexcept {
    termination_requested.test_and_set(std::memory_order_relaxed);
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> arguments;
    if (argc > 1) {
        arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
    }

    const auto controlled_invocation =
        !arguments.empty() &&
        (arguments.front() == "render" || arguments.front() == "pack-revengine" ||
         arguments.front() == "inspect-revengine" ||
         arguments.front() == "verify-revengine");
    if (!controlled_invocation) {
        return engine_sim_offline::cli::run_cli(arguments, std::cout, std::cerr);
    }

    termination_requested.clear(std::memory_order_relaxed);
    const auto previous_interrupt = std::signal(SIGINT, observe_termination_signal);
    const auto previous_termination = std::signal(SIGTERM, observe_termination_signal);
    std::stop_source termination;
    std::jthread signal_bridge{[&](const std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (termination_requested.test(std::memory_order_relaxed)) {
                static_cast<void>(termination.request_stop());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }};

    const auto result = engine_sim_offline::cli::run_cli(
        arguments, std::cout, std::cerr, termination.get_token());
    signal_bridge.request_stop();
    if (previous_interrupt != SIG_ERR) {
        static_cast<void>(std::signal(SIGINT, previous_interrupt));
    }
    if (previous_termination != SIG_ERR) {
        static_cast<void>(std::signal(SIGTERM, previous_termination));
    }
    return result;
}
