#include "simulation/bounded_dyno_constraint.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

namespace contract = crankwave::contract;
namespace detail = crankwave::simulation::detail;
namespace simulation = crankwave::simulation;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error{std::string{message} +
                                 ": actual=" + std::to_string(actual) +
                                 "; expected=" + std::to_string(expected)};
    }
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kDigits[digest.bytes[index] & UINT8_C(0x0f)];
    }
    return result;
}

void test_constraint_method_identities_are_canonical_and_topology_specific() {
    constexpr std::string_view kDirectDigest =
        "e05cf365708017aefb7d53efbfccc9eb86f20cdd4df36b2f0253c7ee0125b28f";
    constexpr std::string_view kMasterRodDigest =
        "e009ba953eca2d206beae16b307a078e18ddf6d31ad99ed3bafe2304ecf9a03a";

    const auto direct_descriptor =
        simulation::bounded_held_dyno_constraint_method_descriptor();
    const auto master_rod_descriptor = simulation::
        bounded_held_dyno_one_level_master_rod_constraint_method_descriptor();
    const auto descriptor_is_canonical = [](std::string_view descriptor) {
        return !descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos;
    };
    expect(descriptor_is_canonical(direct_descriptor),
           "direct held-dyno descriptor is not canonical LF text");
    expect(descriptor_is_canonical(master_rod_descriptor),
           "master-rod held-dyno descriptor is not canonical LF text");

    const auto direct_digest = contract::sha256(std::as_bytes(
        std::span<const char>{direct_descriptor.data(), direct_descriptor.size()}));
    const auto master_rod_digest = contract::sha256(std::as_bytes(std::span<const char>{
        master_rod_descriptor.data(), master_rod_descriptor.size()}));
    expect(digest_hex(direct_digest) == kDirectDigest,
           "direct held-dyno descriptor digest changed: " +
               digest_hex(direct_digest));
    expect(digest_hex(master_rod_digest) == kMasterRodDigest,
           "master-rod held-dyno descriptor digest changed: " +
               digest_hex(master_rod_digest));

    const auto &direct = simulation::bounded_held_dyno_constraint_method_identity();
    const auto &master_rod =
        simulation::bounded_held_dyno_one_level_master_rod_constraint_method_identity();
    expect(direct.id == simulation::kBoundedHeldDynoConstraintMethodId &&
               direct.version == simulation::kBoundedHeldDynoConstraintMethodVersion &&
               direct.configuration_sha256 == direct_digest &&
               contract::validate(direct).ok() &&
               &direct == &simulation::bounded_held_dyno_constraint_method_identity(),
           "direct held-dyno identity changed, is invalid, or is unstable");
    expect(
        master_rod.id ==
                simulation::kBoundedHeldDynoOneLevelMasterRodConstraintMethodId &&
            master_rod.version ==
                simulation::kBoundedHeldDynoOneLevelMasterRodConstraintMethodVersion &&
            master_rod.configuration_sha256 == master_rod_digest &&
            contract::validate(master_rod).ok() &&
            &master_rod ==
                &simulation::
                    bounded_held_dyno_one_level_master_rod_constraint_method_identity(),
        "master-rod held-dyno identity is invalid, detached, or unstable");
    expect(direct.id != master_rod.id &&
               direct.configuration_sha256 != master_rod.configuration_sha256,
           "direct and master-rod held-dyno methods share an identity");

    expect(direct_descriptor.find("exact-centered-slider-crank-M-of-theta") !=
                   std::string_view::npos &&
               direct_descriptor.find("one-level-master-rod") == std::string_view::npos,
           "direct held-dyno descriptor lost its centered-slider-crank boundary");
    expect(master_rod_descriptor.find(
               "exact-articulated-kinetic-energy-coefficient-M-of-theta") !=
                   std::string_view::npos &&
               master_rod_descriptor.find(
                   "leaf-first-coupled-articulated-inverse-dynamics") !=
                   std::string_view::npos &&
               master_rod_descriptor.find(
                   "retained-previous-step-wall-reaction-magnitude") !=
                   std::string_view::npos &&
               master_rod_descriptor.find(
                   "step-n-friction-consumes-wall-reaction-n-minus-one") !=
                   std::string_view::npos &&
               master_rod_descriptor.find(
                   "per-cylinder-piston-travel-chen-flynn-evidence") !=
                   std::string_view::npos,
           "master-rod held-dyno descriptor lost articulated mechanics semantics");
}

[[nodiscard]] const detail::BoundedDynoConstraintStep &
require_step(const detail::BoundedDynoConstraintCalculation &calculation,
             std::string_view message) {
    const auto *step = std::get_if<detail::BoundedDynoConstraintStep>(&calculation);
    expect(step != nullptr, message);
    return *step;
}

void test_constraint_tracks_target_with_absorbing_or_driving_torque() {
    detail::BoundedDynoConstraintInput input{
        2.0, 0.0, {0.5, 100.0}, 200.0, 100.0, 300.0, 100.0, 0.01,
    };
    auto calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &absorbing = require_step(calculation, "absorbing hold was rejected");
    expect(absorbing.disposition == detail::BoundedDynoConstraintDisposition::tracking,
           "available absorbing torque did not track");
    expect_near(absorbing.required_actuator_torque_nm, -200.0, 0.0,
                "absorbing hold required wrong actuator torque");
    expect_near(absorbing.applied_actuator_torque_nm, -200.0, 0.0,
                "absorbing hold applied wrong actuator torque");
    expect_near(absorbing.final_state.angular_speed_rad_s, 100.0, 0.0,
                "absorbing hold missed its target");

    input.held_upstream_engine_torque_nm = -50.0;
    calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &driving = require_step(calculation, "driving hold was rejected");
    expect_near(driving.applied_actuator_torque_nm, 50.0, 0.0,
                "driving hold applied wrong actuator torque");
    expect_near(driving.final_state.angular_speed_rad_s, 100.0, 0.0,
                "driving hold missed its target");
}

void test_constraint_exposes_each_torque_limit() {
    detail::BoundedDynoConstraintInput input{
        2.0, 0.0, {0.0, 100.0}, 500.0, 100.0, 200.0, 100.0, 0.01,
    };
    auto calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &absorbing = require_step(calculation, "absorbing limit was rejected");
    expect(absorbing.disposition ==
               detail::BoundedDynoConstraintDisposition::absorbing_torque_limited,
           "absorbing saturation returned wrong disposition");
    expect_near(absorbing.applied_actuator_torque_nm, -200.0, 0.0,
                "absorbing saturation exceeded its limit");
    expect_near(absorbing.final_state.angular_speed_rad_s, 101.5, 1e-14,
                "absorbing saturation hid achieved speed");

    input.held_upstream_engine_torque_nm = 0.0;
    input.target_angular_speed_rad_s = 102.0;
    calculation = detail::advance_bounded_dyno_constraint(input);
    const auto &driving = require_step(calculation, "driving limit was rejected");
    expect(driving.disposition ==
               detail::BoundedDynoConstraintDisposition::driving_torque_limited,
           "driving saturation returned wrong disposition");
    expect_near(driving.applied_actuator_torque_nm, 100.0, 0.0,
                "driving saturation exceeded its limit");
    expect_near(driving.final_state.angular_speed_rad_s, 100.5, 1e-14,
                "driving saturation hid achieved speed");
}

void test_constraint_compensates_configuration_inertia_term() {
    const auto calculation = detail::advance_bounded_dyno_constraint({
        2.0,
        0.02,
        {0.0, 100.0},
        50.0,
        100.0,
        100.0,
        100.0,
        0.01,
    });
    const auto &step = require_step(calculation, "variable-inertia hold was rejected");
    expect_near(step.velocity_inertia_torque_nm, 100.0, 0.0,
                "velocity-inertia torque was not evaluated");
    expect_near(step.applied_actuator_torque_nm, 50.0, 0.0,
                "constraint did not compensate velocity inertia");
    expect_near(step.final_state.angular_speed_rad_s, 100.0, 0.0,
                "variable-inertia hold missed its target");
}

void test_invalid_and_unpreventable_reverse_are_typed() {
    detail::BoundedDynoConstraintInput input{
        1.0, 0.0, {0.0, 10.0}, -20.0, 10.0, 0.0, 0.0, 1.0,
    };
    const auto stalled = detail::advance_bounded_dyno_constraint(input);
    const auto *stall = std::get_if<detail::BoundedDynoConstraintStall>(&stalled);
    expect(stall != nullptr && stall->predicted_final_angular_speed_rad_s == -10.0 &&
               stall->stall_time_s == 0.5,
           "unpreventable reverse did not return exact stall evidence");

    input.maximum_absorbing_torque_nm = -1.0;
    const auto invalid = detail::advance_bounded_dyno_constraint(input);
    const auto *error = std::get_if<detail::BoundedDynoConstraintInputError>(&invalid);
    expect(error != nullptr && error->issue == detail::BoundedDynoConstraintInputIssue::
                                                   negative_maximum_absorbing_torque,
           "negative absorbing limit returned wrong input issue");
}

void run_tests() {
    test_constraint_method_identities_are_canonical_and_topology_specific();
    test_constraint_tracks_target_with_absorbing_or_driving_torque();
    test_constraint_exposes_each_torque_limit();
    test_constraint_compensates_configuration_inertia_term();
    test_invalid_and_unpreventable_reverse_are_typed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "bounded dyno constraint failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
