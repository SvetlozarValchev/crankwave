#include "engine_sim_offline/profiles/bmw_m52b28_full_throttle_torque_sweep_request.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline;

constexpr std::array<std::string_view,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
    kExpectedScenarioIds{
        "bmw-m52b28-held-1500rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-2500rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-3000rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-3500rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-3950rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-4500rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-5300rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-6000rpm-full-throttle-torque-sweep-v2",
        "bmw-m52b28-held-6500rpm-full-throttle-torque-sweep-v2",
    };

constexpr std::array<std::string_view,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
    kExpectedRequestIdentitySha256{
        "fe531ed35bf4a64e2eb7f7134a366139303925e3bc55b76b8b50bf5930324747",
        "dda12918e53a0a9e406cb7e3779ca48c4f43473df5a9f2b07cea0e78de668bdd",
        "b2c6bbbd9573b6cd500cc12bdd498fef4da2ce1085a07b5e000776cf194482d9",
        "7f4b5fce2bafb989c2a0f98f14d22c2dc61e1abd1d5b1ef7fa9a7d9eab5f1f5a",
        "e0aca4105a4e97344500bbe295c29137727667d3f30800fae2aea5a3ebce5f45",
        "f7c33ebdd1a5998e71628af45e2ccc2f6d47593bb977a66e4f23a563991379cf",
        "68b50fddafeb45300662e9e2d8fc5ebd5d4b773fdb906deadd63ad4fe3a39fa3",
        "8f4f296ad0d0218b856abbda88f164e92376000e2a8a53b999ed82c6e7218faf",
        "57b313b001c1c0b9c97866101b1cc8d54d762c80b2cfb3058b3edd99dec59947",
    };

constexpr std::array<std::string_view,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
    kExpectedProvenanceSha256{
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
        "e663d579e1810423fab96d9989109774939aef8fc9ea85b77275a119e1049f50",
    };

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

template <class T> [[nodiscard]] bool exact(T lhs, T rhs) {
    return std::bit_cast<std::array<std::byte, sizeof(T)>>(lhs) ==
           std::bit_cast<std::array<std::byte, sizeof(T)>>(rhs);
}

[[nodiscard]] profiles::BmwM52b28FullThrottleTorqueSweepRequestSet make_request_set() {
    auto result = profiles::make_bmw_m52b28_full_throttle_torque_sweep_request_set();
    const auto *requests =
        std::get_if<profiles::BmwM52b28FullThrottleTorqueSweepRequestSet>(&result);
    if (requests == nullptr) {
        const auto &report = std::get<contract::ValidationReport>(result);
        std::string message = "canonical BMW torque-sweep request set was rejected";
        for (const auto &issue : report.issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return *requests;
}

[[nodiscard]] identity::SimulationRequestIdentityEncoding
request_identity(const profiles::BmwM52b28FullThrottleTorqueSweepRequest &request) {
    auto result = identity::encode_simulation_request_identity_v3(
        request.engine, request.scenario, request.provenance.bundle);
    const auto *encoding =
        std::get_if<identity::SimulationRequestIdentityEncoding>(&result);
    expect(encoding != nullptr, "canonical torque-sweep identity did not encode");
    return *encoding;
}

void test_exact_frozen_set(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequestSet &requests) {
    expect(
        profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request_set(requests)
            .ok(),
        "canonical torque-sweep request set failed exact revalidation");

    std::array<contract::Sha256Digest,
               profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
        identities{};
    bool request_identity_mismatch = false;
    bool provenance_identity_mismatch = false;
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const auto &request = requests[index];
        expect(profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request(request)
                   .ok(),
               "canonical torque-sweep point failed exact revalidation");
        expect(request.engine.profile_id.value ==
                       "bmw-m52b28-low-order-operating-point-v1" &&
                   request.scenario.engine_profile_id ==
                       request.engine.profile_id.value &&
                   request.scenario.scenario_id == kExpectedScenarioIds[index],
               "canonical torque-sweep engine or scenario identity changed");

        const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
        const auto *preparation = std::get_if<contract::FixedHorizonCycleSampling>(
            &request.scenario.preparation);
        expect(held != nullptr && preparation != nullptr,
               "canonical torque-sweep point lost fixed-horizon sampling");
        expect(
            exact(held->engine_speed_rpm.value,
                  profiles::kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm[index]) &&
                exact(held->throttle_01.value, 1.0) &&
                preparation->trailing_complete_cycle_count.value == 32U &&
                exact(preparation->fixed_preparation_horizon_s.value, 6.44) &&
                preparation->method.value ==
                    contract::fixed_horizon_cycle_sampling_method_identity(),
            "canonical torque-sweep operating point or fixed-horizon policy "
            "changed");

        expect(request.scenario.operating_state.value ==
                   std::vector<contract::OperatingStatePoint>{
                       {"torque-sweep-held-running",
                        0.0,
                        {true, true, false, true, false}},
                   },
               "canonical torque-sweep fired-running state changed");
        expect(
            exact(request.scenario.ambient.pressure_pa_abs.value, 101325.0) &&
                exact(request.scenario.ambient.temperature_k.value, 298.15) &&
                exact(request.scenario.ambient.relative_humidity_01.value, 0.0) &&
                exact(request.scenario.initial_thermal_state.gas_temperature_k.value,
                      298.15) &&
                exact(request.scenario.initial_thermal_state.wall_temperature_k.value,
                      363.15) &&
                exact(
                    request.scenario.initial_thermal_state.coolant_temperature_k.value,
                    363.15) &&
                exact(request.scenario.initial_thermal_state.oil_temperature_k.value,
                      363.15),
            "canonical torque-sweep ambient or thermal conditions changed");
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
                                              request.scenario.rates.physics) ==
                    64400U &&
                contract::resolve_frame_index(
                    request.scenario.audible_duration_s.value,
                    request.scenario.rates.source_processing) == 3840U &&
                contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                              request.scenario.rates.capture) == 64600U,
            "canonical torque-sweep horizons or clocks changed");
        expect(request.scenario.quality.value ==
                       contract::RenderQuality{
                           "low-order-operating-point-torque-sweep-v1",
                           1U,
                           200U,
                           3800U,
                       } &&
                   request.scenario.public_seed.value == UINT64_C(0xC0FFEE),
               "canonical torque-sweep transport or seed changed");

        const auto encoding = request_identity(request);
        expect(encoding.sha256 == contract::sha256(encoding.bytes),
               "canonical torque-sweep identity digest did not cover its bytes");
        identities[index] = encoding.sha256;

        const auto request_sha256 = digest_hex(encoding.sha256);
        if (request_sha256 != kExpectedRequestIdentitySha256[index]) {
            std::cerr << "BMW torque-sweep request " << index
                      << " SHA-256: " << request_sha256 << '\n';
            request_identity_mismatch = true;
        }

        const auto provenance_sha256 = digest_hex(request.provenance.bundle.sha256);
        if (provenance_sha256 != kExpectedProvenanceSha256[index]) {
            std::cerr << "BMW torque-sweep provenance " << index
                      << " SHA-256: " << provenance_sha256 << '\n';
            provenance_identity_mismatch = true;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            expect(identities[index] != identities[earlier],
                   "two canonical torque-sweep points share a request identity");
        }
    }
    expect(!request_identity_mismatch && !provenance_identity_mismatch,
           "canonical torque-sweep request or provenance identities changed");
}

void test_mutation_and_order_rejection(
    const profiles::BmwM52b28FullThrottleTorqueSweepRequestSet &requests) {
    auto changed_point = requests[0];
    std::get<contract::HeldSpeed>(changed_point.scenario.mode).throttle_01.value = 0.99;
    expect(
        !profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request(changed_point)
             .ok(),
        "mutated torque-sweep throttle passed exact point validation");

    auto reordered = requests;
    std::swap(reordered[0], reordered[1]);
    expect(
        !profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request_set(reordered)
             .ok(),
        "reordered torque-sweep points passed exact set validation");

    auto duplicate = requests;
    duplicate[1] = duplicate[0];
    expect(
        !profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request_set(duplicate)
             .ok(),
        "duplicate torque-sweep point passed exact set validation");

    changed_point = requests[0];
    std::get<contract::FixedHorizonCycleSampling>(changed_point.scenario.preparation)
        .trailing_complete_cycle_count.value = 31U;
    expect(
        !profiles::validate_bmw_m52b28_full_throttle_torque_sweep_request(changed_point)
             .ok(),
        "mutated torque-sweep fixed sample size passed exact point validation");
}

void run_tests() {
    const auto requests = make_request_set();
    test_exact_frozen_set(requests);
    test_mutation_and_order_rejection(requests);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "BMW M52B28 full-throttle torque-sweep request test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
