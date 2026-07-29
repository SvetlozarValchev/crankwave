#include "engine_sim_offline/profiles/bmw_m52b28_inertial_dyno_listening_request.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

constexpr std::string_view kExpectedRequestIdentitySha256 =
    "981bc94a449ff0e48b7f4432b17f28f97f95a4ec550fe504c2100c9483e1c92f";
constexpr std::string_view kExpectedProvenanceSha256 =
    "a10d47929043fe4485fea8f6a3354e671ae7bb304006c5f5bad83efe8978ee22";

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
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

[[nodiscard]] profiles::BmwM52b28InertialDynoListeningRequest make_request() {
    auto result = profiles::make_bmw_m52b28_inertial_dyno_listening_request();
    const auto *request =
        std::get_if<profiles::BmwM52b28InertialDynoListeningRequest>(&result);
    if (request == nullptr) {
        const auto &report = std::get<contract::ValidationReport>(result);
        std::string message = "canonical BMW inertial-dyno request was rejected";
        for (const auto &issue : report.issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return *request;
}

void test_exact_request_shape(
    const profiles::BmwM52b28InertialDynoListeningRequest &request) {
    expect(profiles::validate_bmw_m52b28_inertial_dyno_listening_request(request).ok(),
           "canonical BMW inertial-dyno request failed exact revalidation");
    expect(request.engine.profile_id.value ==
                   "bmw-m52b28-low-order-operating-point-v1" &&
               request.scenario.scenario_id ==
                   "bmw-m52b28-inertial-dyno-1500-6500rpm-listening-v2" &&
               request.scenario.engine_profile_id == request.engine.profile_id.value,
           "canonical inertial-dyno engine/scenario identity changed");

    const auto *dyno = std::get_if<contract::InertialDyno>(&request.scenario.mode);
    const auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&request.scenario.preparation);
    expect(dyno != nullptr && preparation != nullptr,
           "canonical request lost inertial-dyno fixed-horizon sampling");
    expect(std::bit_cast<std::uint64_t>(dyno->initial_engine_speed_rpm.value) ==
                   std::bit_cast<std::uint64_t>(1500.0) &&
               std::bit_cast<std::uint64_t>(dyno->target_engine_speed_rpm.value) ==
                   std::bit_cast<std::uint64_t>(6500.0) &&
               std::bit_cast<std::uint64_t>(dyno->equivalent_inertia_kg_m2.value) ==
                   std::bit_cast<std::uint64_t>(7.9) &&
               dyno->throttle_01.interpolation ==
                   contract::TrajectoryInterpolation::right_continuous_hold &&
               dyno->throttle_01.points ==
                   std::vector<contract::ScalarTrajectoryPoint>{{0.0, 0.85}},
           "canonical inertial point, inertia, or throttle changed");

    constexpr double kRadiansPerSecondPerRpm = std::numbers::pi_v<double> / 30.0;
    expect(dyno->brake_curve ==
               std::vector<contract::BrakeTorquePoint>{
                   {1000.0 * kRadiansPerSecondPerRpm, 40.0},
                   {7500.0 * kRadiansPerSecondPerRpm, 40.0},
               },
           "canonical passive brake curve changed");
    expect(dyno->crank_dynamics_method.value.id == "rigid-crank-zoh-work-energy-v1" &&
               dyno->crank_dynamics_method.value.version == 1U &&
               !dyno->crank_dynamics_method.value.configuration_sha256.is_zero() &&
               dyno->brake_torque_method.value.id ==
                   "piecewise-linear-positive-speed-passive-brake-v1" &&
               dyno->brake_torque_method.value.version == 1U &&
               !dyno->brake_torque_method.value.configuration_sha256.is_zero(),
           "canonical inertial-dyno method authority changed");

    expect(preparation->trailing_complete_cycle_count.value == 32U &&
               std::bit_cast<std::uint64_t>(
                   preparation->fixed_preparation_horizon_s.value) ==
                   std::bit_cast<std::uint64_t>(6.44) &&
               preparation->method.value ==
                   contract::fixed_horizon_cycle_sampling_method_identity(),
           "canonical inertial fixed-horizon sampling policy changed");
    expect(request.scenario.operating_state.value ==
               std::vector<contract::OperatingStatePoint>{
                   {"inertial-dyno-running", 0.0, {true, true, false, true, false}},
               },
           "canonical fired dyno-running state changed");

    expect(
        request.scenario.rates ==
                contract::RenderRates{
                    {10000U, 1U},
                    {10000U, 1U},
                    {192000U, 1U},
                    {192000U, 1U},
                    {192000U, 1U},
                } &&
            contract::resolve_frame_index(request.scenario.audible_start_s.value,
                                          request.scenario.rates.physics) == 64400U &&
            contract::resolve_frame_index(request.scenario.audible_duration_s.value,
                                          request.scenario.rates.source_processing) ==
                2880000U &&
            contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                          request.scenario.rates.capture) == 214400U &&
            contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                          request.scenario.rates.delivery) == 4116480U,
        "canonical inertial-dyno horizons or clocks changed");
    expect(request.scenario.quality.value.profile_id ==
                   "low-order-operating-point-listening-v1" &&
               request.scenario.quality.value.capture_block_capacity_frames == 200U &&
               request.scenario.quality.value.event_journal_capacity_records == 3800U &&
               request.scenario.public_seed.value == UINT64_C(0xC0FFEE),
           "canonical inertial-dyno quality, transport, or seed changed");

    expect(std::ranges::all_of(
               request.provenance.resolutions,
               [](const auto &resolution) {
                   return resolution.id.starts_with(
                              "bmw-m52b28-operating-profile-resolution-") &&
                          resolution.parameter_path.find("legacy-low-order-v1") ==
                              std::string::npos;
               }),
           "inertial-dyno request reused an M3 resolution identity or root");
}

void test_identity_and_exact_mutation_rejection(
    const profiles::BmwM52b28InertialDynoListeningRequest &request) {
    const auto identity_result = identity::encode_simulation_request_identity_v3(
        request.engine, request.scenario, request.provenance.bundle);
    const auto *identity =
        std::get_if<identity::SimulationRequestIdentityEncoding>(&identity_result);
    expect(identity != nullptr, "canonical inertial-dyno identity did not encode");
    expect(identity->sha256 == contract::sha256(identity->bytes),
           "inertial-dyno request identity digest did not cover its bytes");
    const auto request_sha256 = digest_hex(identity->sha256);
    if (request_sha256 != kExpectedRequestIdentitySha256) {
        std::cerr << "BMW inertial-dyno request SHA-256: " << request_sha256 << '\n';
    }
    const auto provenance_sha256 = digest_hex(request.provenance.bundle.sha256);
    if (provenance_sha256 != kExpectedProvenanceSha256) {
        std::cerr << "BMW inertial-dyno provenance SHA-256: " << provenance_sha256
                  << '\n';
    }
    expect(request_sha256 == kExpectedRequestIdentitySha256 &&
               provenance_sha256 == kExpectedProvenanceSha256,
           "inertial-dyno request or provenance identity changed");

    auto second = make_request();
    expect(second == request,
           "canonical inertial-dyno factory did not reproduce exact bytes");

    auto changed = request;
    std::get<contract::InertialDyno>(changed.scenario.mode)
        .target_engine_speed_rpm.value = 6499.0;
    expect(!profiles::validate_bmw_m52b28_inertial_dyno_listening_request(changed).ok(),
           "mutated inertial target passed exact validation");

    changed = request;
    std::get<contract::InertialDyno>(changed.scenario.mode)
        .brake_curve[0]
        .resisting_torque_nm = 39.0;
    expect(!profiles::validate_bmw_m52b28_inertial_dyno_listening_request(changed).ok(),
           "mutated passive brake passed exact validation");

    changed = request;
    std::get<contract::FixedHorizonCycleSampling>(changed.scenario.preparation)
        .trailing_complete_cycle_count.value = 31U;
    expect(!profiles::validate_bmw_m52b28_inertial_dyno_listening_request(changed).ok(),
           "mutated fixed-horizon sample size passed exact validation");
}

void run_tests() {
    const auto request = make_request();
    test_exact_request_shape(request);
    test_identity_and_exact_mutation_rejection(request);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
