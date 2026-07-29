#include "reference/bmw_m52b28_held_settling_diagnostic.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

int run(int argc) {
    if (argc != 1) {
        throw std::invalid_argument{
            "usage: engine-sim-offline-m4-bmw-held-settling-diagnostic"};
    }
    engine_sim_offline::reference::
        run_bmw_m52b28_cross_rpm_fixed_sample_diagnostic(std::cout);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **) {
    try {
        return run(argc);
    } catch (const std::exception &error) {
        std::cerr << "M4 BMW cross-RPM fixed-sample diagnostic failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
