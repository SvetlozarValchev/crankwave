#include "simulation/engine_sim_v1_transient_friction.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline::simulation;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void test_positive_speed_crank_friction_matches_pristine_constraint_limit() {
    constexpr double kM52TenPoundFeetNm =
        10.0 * (4.44822 * ((1.0 / 100.0) * 2.54 * 12.0));
    const auto calculation =
        calculate_engine_sim_v1_positive_speed_crank_friction({kM52TenPoundFeetNm});
    const auto *result =
        std::get_if<EngineSimV1PositiveSpeedCrankFriction>(&calculation);
    expect(result != nullptr && std::bit_cast<std::uint64_t>(result->torque_nm) ==
                                    std::bit_cast<std::uint64_t>(-kM52TenPoundFeetNm),
           "positive-running crank friction did not saturate at the pristine "
           "opposing torque limit");

    const auto zero = calculate_engine_sim_v1_positive_speed_crank_friction({0.0});
    const auto *zero_result = std::get_if<EngineSimV1PositiveSpeedCrankFriction>(&zero);
    expect(zero_result != nullptr && zero_result->torque_nm == 0.0,
           "zero authored crank friction was rejected or changed");
}

void test_invalid_authored_crank_friction_is_typed() {
    const auto nonfinite = calculate_engine_sim_v1_positive_speed_crank_friction(
        {std::numeric_limits<double>::infinity()});
    const auto *nonfinite_error =
        std::get_if<EngineSimV1CrankFrictionError>(&nonfinite);
    expect(nonfinite_error != nullptr &&
               nonfinite_error->issue ==
                   EngineSimV1CrankFrictionIssue::nonfinite_running_friction_torque,
           "nonfinite authored crank friction returned the wrong error");

    const auto negative = calculate_engine_sim_v1_positive_speed_crank_friction({-1.0});
    const auto *negative_error = std::get_if<EngineSimV1CrankFrictionError>(&negative);
    expect(negative_error != nullptr &&
               negative_error->issue ==
                   EngineSimV1CrankFrictionIssue::negative_running_friction_torque,
           "negative authored crank friction returned the wrong error");
}

} // namespace

int main() {
    try {
        test_positive_speed_crank_friction_matches_pristine_constraint_limit();
        test_invalid_authored_crank_friction_is_typed();
    } catch (const std::exception &error) {
        std::cerr << "engine-sim v1 transient friction failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
