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
        "bmw-m52b28-held-1500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-2500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-3000rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-3500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-3950rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-4500rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-5300rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-6000rpm-full-throttle-torque-sweep-v1",
        "bmw-m52b28-held-6500rpm-full-throttle-torque-sweep-v1",
    };

constexpr std::array<std::string_view,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
    kExpectedRequestIdentitySha256{
        "bfdf6b406049743ea09b591207d71c07638b63e603a2cd9d95a8b20951649bae",
        "d86dd9f6b2465736f62c9d5d00ffb2106f872208fa29eef0e71080bf3cb35409",
        "a5c5f1e2dedab56dd7229b70c41b36868ce9cdab896556cfcd9a6d86e318bb1e",
        "b49a97a71f54710e61a85769be10c34296ee0d5fa0d8d2c94f6f2e6dc32e05ec",
        "b29355d50d77ddee69e94696c3af389de5a47fffdc7c22d8f94f7d480a806d65",
        "89583f2ecb8c66481b818c745c855ca798ff66f5ab1a33ba55a479500c4b4920",
        "e772038c2a1b122caa03dcff668a1e9ea76b606f69611a0227f7a37cc4e100fb",
        "c57680d6365fbb4a671a2e54bfa46b5c2b33405a863c25676b1e4b6c5f18c4ca",
        "0f4cd3443423d315d60d46157ba044359bcb9138d3e4ec471643f0c2af3f30ec",
    };

constexpr std::array<std::string_view,
                     profiles::kBmwM52b28FullThrottleTorqueSweepPointCount>
    kExpectedProvenanceSha256{
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
        "05bdb20fa7b3039346b2eb8710739d62c865328dff6a43e83d62396049701d60",
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
    auto result = identity::encode_simulation_request_identity_v2(
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
        const auto *preparation =
            std::get_if<contract::ConvergenceSettling>(&request.scenario.preparation);
        expect(held != nullptr && preparation != nullptr,
               "canonical torque-sweep point lost held-speed convergence mode");
        expect(
            exact(held->engine_speed_rpm.value,
                  profiles::kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm[index]) &&
                exact(held->throttle_01.value, 1.0) &&
                preparation->comparison_cycle_count.value == 16U &&
                exact(preparation->maximum_preparation_duration_s.value, 6.44) &&
                exact(preparation->cycle_mean_torque_tolerance_nm.value, 0.75) &&
                exact(preparation->pressure_tolerance_pa.value, 1500.0),
            "canonical torque-sweep operating point or convergence policy "
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
        }
        expect(request_sha256 == kExpectedRequestIdentitySha256[index],
               "canonical torque-sweep request identity changed");

        const auto provenance_sha256 = digest_hex(request.provenance.bundle.sha256);
        if (provenance_sha256 != kExpectedProvenanceSha256[index]) {
            std::cerr << "BMW torque-sweep provenance " << index
                      << " SHA-256: " << provenance_sha256 << '\n';
        }
        expect(provenance_sha256 == kExpectedProvenanceSha256[index],
               "canonical torque-sweep provenance identity changed");
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            expect(identities[index] != identities[earlier],
                   "two canonical torque-sweep points share a request identity");
        }
    }
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
