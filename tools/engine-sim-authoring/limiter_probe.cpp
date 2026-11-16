// Inspect actual native engine-sim limiter transitions for the same control tape
// used by audition.py. This is an offline verification tool, not a sound layer.
#include "engine_sim_web_api.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: limiter-probe script.mr controls.txt events.jsonl");
        using Runtime = std::unique_ptr<esw_runtime, decltype(&esw_destroy)>;
        Runtime runtime(esw_create(argv[1], 20261104), esw_destroy);
        if (!runtime) throw std::runtime_error(esw_get_global_error());
        std::ifstream controls(argv[2]);
        std::ofstream output(argv[3]);
        if (!controls || !output) throw std::runtime_error("Cannot open control tape or output");
        output << std::setprecision(17);
        const auto frequency = esw_get_simulation_frequency(runtime.get());
        output << "{\"kind\":\"metadata\",\"physics_hz\":" << frequency
               << ",\"activation_rpm\":" << esw_get_rev_limit_rpm(runtime.get())
               << ",\"cut_seconds\":" << esw_get_rev_limiter_duration_seconds(runtime.get()) << "}\n";
        std::string line;
        uint32_t expected = 0;
        std::vector<int16_t> pcm;
        while (std::getline(controls, line)) {
            if (line.empty()) continue;
            std::istringstream fields(line);
            uint32_t index, ticks;
            double throttle, target, torque;
            int ignition, starter, dyno, hold;
            std::string extra;
            if (!(fields >> index >> ticks >> throttle >> ignition >> starter >> dyno >> hold >> target >> torque)
                || fields >> extra || index != expected++ || ticks == 0
                || ticks > esw_get_max_ticks_per_advance(runtime.get())) {
                throw std::runtime_error("Invalid control tape");
            }
            if (!esw_set_controls(runtime.get(), throttle, ignition, starter, dyno, hold, target, torque)
                || !esw_advance(runtime.get(), ticks)) {
                throw std::runtime_error(esw_get_last_error(runtime.get()));
            }
            while (const auto frames = esw_pcm_available(runtime.get())) {
                pcm.resize(frames);
                if (esw_read_pcm(runtime.get(), pcm.data(), frames) != frames)
                    throw std::runtime_error("Short PCM drain");
            }
            while (esw_limiter_events_available(runtime.get())) {
                esw_limiter_event event{};
                event.struct_size = ESW_LIMITER_EVENT_SIZE;
                if (esw_read_limiter_event(runtime.get(), &event) != 1 || !std::isfinite(event.raw_rpm))
                    throw std::runtime_error("Invalid limiter event");
                output << "{\"kind\":\""
                       << (event.kind == ESW_LIMITER_CUT_BEGIN ? "cut_begin" : "cut_end")
                       << "\",\"tick\":" << event.physics_tick
                       << ",\"seconds\":" << double(event.physics_tick) / frequency
                       << ",\"sequence\":" << event.sequence
                       << ",\"rpm\":" << event.raw_rpm << "}\n";
            }
        }
        if (!expected || !controls.eof() || !output)
            throw std::runtime_error("Incomplete control tape or event output");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
