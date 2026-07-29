#include "contract_test_support.hpp"

#include <exception>
#include <iostream>

int main() {
    using namespace engine_sim_offline::contract::test;

    try {
        run_primitives_contract_tests();
        run_authored_profile_contract_tests();
        run_parity_model_contract_tests();
        run_exhaust_acoustics_contract_tests();
        run_capture_contract_tests();
        run_randomness_contract_tests();
        run_scenario_manifest_contract_tests();
    } catch (const std::exception &error) {
        std::cerr << "contract test failure: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
