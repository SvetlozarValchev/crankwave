#include "engine_sim_offline/contract/result.hpp"

#include "contract_test_support.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace engine_sim_offline::contract;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void expect_valid(const ValidationReport &report, std::string_view message) {
    if (report.ok()) {
        return;
    }
    std::string detail{message};
    for (const auto &issue : report.issues) {
        detail += "\n" + issue.path + ": " + issue.message;
    }
    throw std::runtime_error{detail};
}

Sha256Digest digest(std::uint8_t first) {
    Sha256Digest value;
    value.bytes.front() = first;
    return value;
}

MethodIdentity method(std::string_view id, std::uint8_t digest_byte) {
    return MethodIdentity{std::string{id}, 1, digest(digest_byte)};
}

double kinetic_energy_change(double inertia_kg_m2, double start_rpm, double end_rpm) {
    constexpr double radians_per_second_per_rpm = std::numbers::pi_v<double> / 30.0;
    const double start = start_rpm * radians_per_second_per_rpm;
    const double end = end_rpm * radians_per_second_per_rpm;
    return 0.5 * inertia_kg_m2 * (end * end - start * start);
}

RenderScenario request(const Sha256Digest &) {
    RenderScenario value;
    value.audible_start_s.value = 1.0;
    value.total_duration_s.value = 3.0;
    value.rates.physics = {100, 1};

    InertialDyno dyno;
    dyno.initial_engine_speed_rpm.value = 1000.0;
    dyno.target_engine_speed_rpm.value = 1800.0;
    dyno.equivalent_inertia_kg_m2.value = 2.0;
    dyno.brake_curve = {{0.0, 0.0}, {220.0, 50.0}};
    dyno.brake_curve_resolution_id = "brake-curve-resolution";
    dyno.brake_torque_method.value = method("linear-passive-brake", 2);
    dyno.crank_dynamics_method.value = method("semi-implicit-crank", 3);
    value.mode = std::move(dyno);
    return value;
}

InertialDynoResult result(const Sha256Digest &request_digest) {
    InertialDynoResult value;
    value.simulation_request_identity_v4_sha256 = request_digest;
    value.start_engine_speed_rpm = 1000.0;
    value.target_engine_speed_rpm = 1800.0;
    value.release_engine_speed_rpm = 1000.0;
    // The pull reached its target and then slowed before the fixed horizon. This is
    // deliberately valid: reaching the target never truncates the audio artifact.
    value.end_engine_speed_rpm = 1700.0;
    value.minimum_engine_speed_rpm = 1000.0;
    value.maximum_engine_speed_rpm = 1900.0;
    value.release_frame_index = 100;
    value.end_frame_index = 300;
    value.first_target_reached_frame_index = 200;
    value.equivalent_inertia_kg_m2 = 2.0;
    value.brake_curve_resolution_id = "brake-curve-resolution";
    value.brake_torque_method = method("linear-passive-brake", 2);
    value.crank_dynamics_method = method("semi-implicit-crank", 3);
    value.energy_balance.kinetic_energy_change_j = kinetic_energy_change(
        value.equivalent_inertia_kg_m2, value.release_engine_speed_rpm,
        value.end_engine_speed_rpm);
    value.energy_balance.passive_brake_absorbed_work_j = 100.0;
    value.energy_balance.net_shaft_work_j =
        value.energy_balance.passive_brake_absorbed_work_j +
        value.energy_balance.kinetic_energy_change_j;
    value.energy_balance.residual_j = 0.0;
    return value;
}

void test_valid_fixed_horizon_result() {
    const auto request_digest = digest(1);
    const auto scenario = request(request_digest);
    const auto evidence = result(request_digest);
    expect(validate(evidence).ok(), "valid standalone inertial result was rejected");
    expect(validate(evidence, scenario, request_digest).ok(),
           "valid request-bound inertial result was rejected");
}

void test_target_and_energy_claims_are_not_free_scalars() {
    const auto request_digest = digest(1);

    auto missing_crossing = result(request_digest);
    missing_crossing.first_target_reached_frame_index.reset();
    expect(!validate(missing_crossing).ok(),
           "target crossing was omitted despite a maximum above target");

    auto forged_residual = result(request_digest);
    forged_residual.energy_balance.residual_j = 1.0;
    expect(!validate(forged_residual).ok(),
           "energy residual drifted from its retained work terms");

    auto forged_kinetic_energy = result(request_digest);
    forged_kinetic_energy.energy_balance.kinetic_energy_change_j += 1.0;
    forged_kinetic_energy.energy_balance.net_shaft_work_j += 1.0;
    expect(!validate(forged_kinetic_energy).ok(),
           "kinetic-energy change drifted from inertia and endpoint speeds");
}

void test_request_binding_and_fixed_frames() {
    const auto request_digest = digest(1);
    const auto scenario = request(request_digest);

    auto wrong_release = result(request_digest);
    ++wrong_release.release_frame_index;
    expect(!validate(wrong_release, scenario, request_digest).ok(),
           "inertial release frame drifted from audible start");

    auto wrong_method = result(request_digest);
    wrong_method.brake_torque_method.configuration_sha256 = digest(9);
    expect(!validate(wrong_method, scenario, request_digest).ok(),
           "brake method drifted from the request");

    auto outside_curve = result(request_digest);
    outside_curve.maximum_engine_speed_rpm = 2200.0;
    expect(!validate(outside_curve, scenario, request_digest).ok(),
           "reported pull extrapolated beyond the passive brake curve");

    auto wrong_digest = result(digest(8));
    expect(!validate(wrong_digest, scenario, request_digest).ok(),
           "inertial result was accepted against a different request digest");
}

void test_inertial_request_has_one_exact_release_and_identified_brake_method() {
    engine_sim_offline::contract::test::InputBuilder builder;
    auto content = engine_sim_offline::contract::test::make_manifest_content(builder);
    auto scenario =
        engine_sim_offline::contract::test::simulation_inputs(content).scenario;
    const auto held = std::get<HeldSpeed>(scenario.mode);
    const auto throttle_resolution_id = held.throttle_01.resolution_id;
    std::get<FixedHorizonCycleSampling>(scenario.preparation)
        .trailing_complete_cycle_count.value = 4;

    InertialDyno dyno;
    dyno.initial_engine_speed_rpm =
        builder.resolved(1000.0, "scenario.mode.initial_engine_speed_rpm");
    dyno.initial_theta_rad = held.initial_theta_rad;
    dyno.equivalent_inertia_kg_m2 =
        builder.resolved(2.0, "scenario.mode.equivalent_inertia_kg_m2");
    dyno.throttle_01 = {
        TrajectoryInterpolation::right_continuous_hold,
        {{0.0, 0.85}},
        throttle_resolution_id,
    };
    dyno.brake_curve = {{0.0, 0.0}, {250.0, 50.0}};
    dyno.brake_curve_resolution_id =
        builder.add_resolution("scenario.mode.brake_curve");
    dyno.crank_dynamics_method = builder.resolved(
        method("semi-implicit-crank", 3), "scenario.mode.crank_dynamics_method");
    dyno.target_engine_speed_rpm =
        builder.resolved(1800.0, "scenario.mode.target_engine_speed_rpm");
    dyno.brake_torque_method = builder.resolved(method("linear-passive-brake", 2),
                                                "scenario.mode.brake_torque_method");
    scenario.mode = std::move(dyno);

    expect_valid(validate(scenario, builder.provenance),
                 "valid inertial request with exact release was rejected");

    auto early_release = scenario;
    std::get<FixedHorizonCycleSampling>(early_release.preparation)
        .fixed_preparation_horizon_s.value = 1.99;
    expect(!validate(early_release, builder.provenance).ok(),
           "inertial request admitted a sampling horizon before audible release");

    auto unidentified_brake = scenario;
    std::get<InertialDyno>(unidentified_brake.mode).brake_torque_method.value = {};
    expect(!validate(unidentified_brake, builder.provenance).ok(),
           "inertial request admitted an unidentified brake evaluator");

    auto unbracketed_target = scenario;
    std::get<InertialDyno>(unbracketed_target.mode)
        .brake_curve.back()
        .angular_speed_rad_s = 150.0;
    expect(!validate(unbracketed_target, builder.provenance).ok(),
           "inertial request admitted brake-curve extrapolation at its target");
}

} // namespace

int main() {
    try {
        test_valid_fixed_horizon_result();
        test_target_and_energy_claims_are_not_free_scalars();
        test_request_binding_and_fixed_frames();
        test_inertial_request_has_one_exact_release_and_identified_brake_method();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
