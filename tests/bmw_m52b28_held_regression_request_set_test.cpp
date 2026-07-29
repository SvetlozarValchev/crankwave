#include "engine_sim_offline/profiles/bmw_m52b28_held_regression_request.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

struct ExpectedPoint {
    std::string_view point_key;
    std::string_view scenario_id;
    double engine_speed_rpm;
    double throttle_01;
    std::uint64_t first_completed_cycle_ordinal;
    std::uint64_t last_completed_cycle_ordinal;
    std::string_view request_identity_sha256;
    std::string_view provenance_sha256;
};

constexpr std::array<ExpectedPoint, profiles::kBmwM52b28HeldRegressionPointCount>
    kExpectedPoints{{
        {
            "rpm1500-throttle0p85",
            "bmw-m52b28-held-regression-rpm1500-throttle0p85",
            1500.0,
            0.85,
            47U,
            78U,
            "3fcfbdc310f9cb0c3417fd21160267b2ff52497bdd503baa9e67bbe2e3188878",
            "0e24e820683ef92658971c5641393fe21cb08e9a942c8f38cf0ab75d4ec08825",
        },
        {
            "rpm3000-throttle0p25",
            "bmw-m52b28-held-regression-rpm3000-throttle0p25",
            3000.0,
            0.25,
            127U,
            158U,
            "848cce413cda58e5026bb370f7fe534082bf1ee03d522997300533acb062b427",
            "0e24e820683ef92658971c5641393fe21cb08e9a942c8f38cf0ab75d4ec08825",
        },
        {
            "rpm3000-throttle0p85",
            "bmw-m52b28-held-regression-rpm3000-throttle0p85",
            3000.0,
            0.85,
            127U,
            158U,
            "96c7338e67ece2d806c530097a66dfd73219e8c7a63d5f15a0424d9fc65a1172",
            "0e24e820683ef92658971c5641393fe21cb08e9a942c8f38cf0ab75d4ec08825",
        },
        {
            "rpm6500-throttle0p85",
            "bmw-m52b28-held-regression-rpm6500-throttle0p85",
            6500.0,
            0.85,
            315U,
            346U,
            "49feaf859f358e27f0bcf48897f32e7104dfbc2399a9341a745054c79c968421",
            "0e24e820683ef92658971c5641393fe21cb08e9a942c8f38cf0ab75d4ec08825",
        },
    }};

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

[[nodiscard]] profiles::BmwM52b28HeldRegressionRequestSet make_request_set() {
    auto result = profiles::make_bmw_m52b28_held_regression_request_set();
    const auto *request_set =
        std::get_if<profiles::BmwM52b28HeldRegressionRequestSet>(&result);
    if (request_set == nullptr) {
        const auto &report = std::get<contract::ValidationReport>(result);
        std::string message = "canonical BMW held-regression request set was rejected";
        for (const auto &issue : report.issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return *request_set;
}

[[nodiscard]] identity::SimulationRequestIdentityEncoding
request_identity(const profiles::BmwM52b28HeldRegressionRequest &request) {
    auto result = identity::encode_simulation_request_identity_v3(
        request.engine, request.scenario, request.provenance.bundle);
    const auto *encoding =
        std::get_if<identity::SimulationRequestIdentityEncoding>(&result);
    expect(encoding != nullptr,
           "canonical BMW held-regression request identity did not encode");
    return *encoding;
}

void test_exact_set_shape(
    const profiles::BmwM52b28HeldRegressionRequestSet &request_set) {
    expect(request_set.size() == profiles::kBmwM52b28HeldRegressionPointCount &&
               request_set.size() == kExpectedPoints.size(),
           "canonical held-regression set extent changed");
    expect(profiles::validate_bmw_m52b28_held_regression_request_set(request_set).ok(),
           "canonical held-regression set failed exact revalidation");

    for (std::size_t index = 0; index < request_set.size(); ++index) {
        const auto &request = request_set[index];
        const auto &expected = kExpectedPoints[index];
        expect(request.point_key == expected.point_key &&
                   request.scenario.scenario_id == expected.scenario_id &&
                   request.engine.profile_id.value ==
                       "bmw-m52b28-low-order-operating-point-v1" &&
                   request.scenario.engine_profile_id ==
                       request.engine.profile_id.value,
               "canonical held-regression point identity or order changed");

        const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
        const auto *preparation = std::get_if<contract::FixedHorizonCycleSampling>(
            &request.scenario.preparation);
        expect(held != nullptr && preparation != nullptr,
               "canonical matrix point lost held-speed fixed-horizon sampling");
        expect(std::bit_cast<std::uint64_t>(held->engine_speed_rpm.value) ==
                       std::bit_cast<std::uint64_t>(expected.engine_speed_rpm) &&
                   std::bit_cast<std::uint64_t>(held->throttle_01.value) ==
                       std::bit_cast<std::uint64_t>(expected.throttle_01) &&
                   preparation->trailing_complete_cycle_count.value == 32U &&
                   std::bit_cast<std::uint64_t>(
                       preparation->fixed_preparation_horizon_s.value) ==
                       std::bit_cast<std::uint64_t>(6.44) &&
                   preparation->method.value ==
                       contract::fixed_horizon_cycle_sampling_method_identity(),
               "canonical matrix operating point or sampling policy changed");

        expect(
            request.scenario.operating_state.value ==
                std::vector<contract::OperatingStatePoint>{
                    {"held-regression-running", 0.0, {true, true, false, true, false}},
                },
            "canonical held-regression running state changed");
        expect(
            request.scenario.rates ==
                    contract::RenderRates{
                        {10000U, 1U},
                        {10000U, 1U},
                        {192000U, 1U},
                        {192000U, 1U},
                        {192000U, 1U},
                    } &&
                std::bit_cast<std::uint64_t>(request.scenario.audible_start_s.value) ==
                    std::bit_cast<std::uint64_t>(6.44) &&
                std::bit_cast<std::uint64_t>(
                    request.scenario.audible_duration_s.value) ==
                    std::bit_cast<std::uint64_t>(15.0) &&
                std::bit_cast<std::uint64_t>(request.scenario.total_duration_s.value) ==
                    std::bit_cast<std::uint64_t>(21.44) &&
                contract::resolve_frame_index(request.scenario.audible_start_s.value,
                                              request.scenario.rates.physics) ==
                    64400U &&
                contract::resolve_frame_index(
                    request.scenario.audible_duration_s.value,
                    request.scenario.rates.source_processing) == 2880000U &&
                contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                              request.scenario.rates.capture) ==
                    214400U &&
                contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                              request.scenario.rates.delivery) ==
                    4116480U,
            "canonical held-regression horizons or clocks changed");
        expect(request.scenario.quality.value.capture_block_capacity_frames == 200U &&
                   request.scenario.quality.value.event_journal_capacity_records ==
                       3800U &&
                   request.scenario.public_seed.value == UINT64_C(0xC0FFEE),
               "canonical held-regression transport or seed changed");

        expect(std::bit_cast<std::uint64_t>(
                   request.scenario.ambient.pressure_pa_abs.value) ==
                       std::bit_cast<std::uint64_t>(101325.0) &&
                   std::bit_cast<std::uint64_t>(
                       request.scenario.ambient.temperature_k.value) ==
                       std::bit_cast<std::uint64_t>(298.15) &&
                   std::bit_cast<std::uint64_t>(
                       request.scenario.ambient.relative_humidity_01.value) ==
                       std::bit_cast<std::uint64_t>(0.0) &&
                   request.scenario.initial_thermal_state ==
                       request_set.front().scenario.initial_thermal_state &&
                   request.scenario.crankcase ==
                       request_set.front().scenario.crankcase &&
                   request.scenario.fuel == request_set.front().scenario.fuel,
               "canonical matrix points no longer share operating conditions");

        expect(std::ranges::all_of(
                   request.provenance.resolutions,
                   [](const auto &resolution) {
                       return resolution.id.starts_with(
                                  "bmw-m52b28-operating-profile-resolution-") &&
                              resolution.parameter_path.find("legacy-low-order-v1") ==
                                  std::string::npos;
                   }),
               "held-regression request reused an M3 resolution identity or root");
    }
}

void test_identities(const profiles::BmwM52b28HeldRegressionRequestSet &request_set) {
    bool all_goldens_match = true;
    std::array<contract::Sha256Digest, profiles::kBmwM52b28HeldRegressionPointCount>
        request_digests{};
    std::array<contract::Sha256Digest, profiles::kBmwM52b28HeldRegressionPointCount>
        provenance_digests{};

    for (std::size_t index = 0; index < request_set.size(); ++index) {
        const auto identity = request_identity(request_set[index]);
        expect(identity.sha256 == contract::sha256(identity.bytes),
               "held-regression identity digest did not cover its bytes");
        request_digests[index] = identity.sha256;
        provenance_digests[index] = request_set[index].provenance.bundle.sha256;

        const auto request_sha256 = digest_hex(request_digests[index]);
        const auto provenance_sha256 = digest_hex(provenance_digests[index]);
        if (request_sha256 != kExpectedPoints[index].request_identity_sha256 ||
            provenance_sha256 != kExpectedPoints[index].provenance_sha256) {
            std::cerr << "BMW held-regression point " << index
                      << " request SHA-256: " << request_sha256
                      << "\nBMW held-regression point " << index
                      << " provenance SHA-256: " << provenance_sha256 << '\n';
            all_goldens_match = false;
        }
    }

    expect(std::ranges::adjacent_find(request_digests) == request_digests.end(),
           "held-regression points reused adjacent request identities");
    for (std::size_t left = 0; left < request_set.size(); ++left) {
        for (std::size_t right = left + 1U; right < request_set.size(); ++right) {
            expect(request_digests[left] != request_digests[right],
                   "held-regression points do not own unique request identities");
        }
    }
    expect(all_goldens_match, "held-regression request or provenance identity changed");
}

void test_mutation_and_reorder_rejection(
    const profiles::BmwM52b28HeldRegressionRequestSet &request_set) {
    for (std::size_t index = 0; index < request_set.size(); ++index) {
        auto changed = request_set;
        changed[index].point_key += "-mutated";
        expect(!profiles::validate_bmw_m52b28_held_regression_request_set(changed).ok(),
               "mutated held-regression point key passed exact validation");

        changed = request_set;
        changed[index].scenario.scenario_id += "-mutated";
        expect(!profiles::validate_bmw_m52b28_held_regression_request_set(changed).ok(),
               "mutated held-regression scenario identity passed exact validation");

        changed = request_set;
        std::get<contract::HeldSpeed>(changed[index].scenario.mode).throttle_01.value =
            0.5;
        expect(!profiles::validate_bmw_m52b28_held_regression_request_set(changed).ok(),
               "mutated held-regression throttle passed exact validation");
    }

    auto reordered = request_set;
    std::swap(reordered[0], reordered[1]);
    expect(!profiles::validate_bmw_m52b28_held_regression_request_set(reordered).ok(),
           "reordered held-regression set passed exact validation");

    auto duplicated = request_set;
    duplicated[3] = duplicated[2];
    expect(!profiles::validate_bmw_m52b28_held_regression_request_set(duplicated).ok(),
           "duplicated held-regression point passed exact validation");

    auto changed_sample = request_set;
    std::get<contract::FixedHorizonCycleSampling>(
        changed_sample[2].scenario.preparation)
        .trailing_complete_cycle_count.value = 31U;
    expect(
        !profiles::validate_bmw_m52b28_held_regression_request_set(changed_sample).ok(),
        "mutated held-regression sample size passed exact validation");
}

[[nodiscard]] bool complete_finite_torque(const contract::TorqueValueNm &torque) {
    return std::isfinite(torque.value_nm) &&
           torque.availability == contract::Availability::available &&
           torque.completeness == contract::Completeness::complete &&
           torque.omitted_terms == 0U;
}

void test_fixed_horizon_sample(const profiles::BmwM52b28HeldRegressionRequest &request,
                               const ExpectedPoint &expected) {
    const auto identity = request_identity(request);
    auto plan_result =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
    const auto *plan = std::get_if<simulation::LowOrderCapturePlan>(&plan_result);
    expect(plan != nullptr, "held-regression plan did not compile");

    auto operating_result = simulation::compile_low_order_operating_point_v1_runtime(
        request.engine, request.scenario, *plan, identity.sha256);
    auto *operating =
        std::get_if<simulation::LowOrderOperatingPointV1Runtime>(&operating_result);
    expect(operating != nullptr, "held-regression runtime did not compile");

    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        request.engine.physics_profile);
    auto core_result = simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, profile.core);
    auto *core = std::get_if<simulation::LowOrderEngineCoreV1Runtime>(&core_result);
    expect(core != nullptr, "held-regression engine core did not compile");

    while (operating->accepted_sample_count() <
           operating->fixed_preparation_horizon_frame_count()) {
        auto step_result = core->advance();
        const auto *step =
            std::get_if<simulation::LowOrderEngineCoreV1StepView>(&step_result);
        expect(step != nullptr, "engine core ended before the fixed horizon");
        const auto advance = operating->advance(step->mechanics.get(), step->gas.get());
        if (const auto *failure = std::get_if<contract::FailureContext>(&advance)) {
            throw std::runtime_error{"held-regression fixed-horizon run failed: " +
                                     failure->state_summary};
        }
    }

    expect(operating->finalized() && !operating->faulted() &&
               operating->operating_point_result().has_value(),
           "held-regression fixed-horizon run did not publish an operating point");
    const auto &point = *operating->operating_point_result();
    expect(contract::validate(point, request.scenario, request.engine, identity.sha256)
               .ok(),
           "held-regression result failed request-bound validation");
    const auto &sample = point.sampling.trailing_complete_cycles.cycles;
    const auto &block = point.reported_block();
    if (sample.first_completed_cycle_ordinal !=
            expected.first_completed_cycle_ordinal ||
        sample.last_completed_cycle_ordinal != expected.last_completed_cycle_ordinal) {
        std::cerr << expected.point_key
                  << " sample cycle range: " << sample.first_completed_cycle_ordinal
                  << ".." << sample.last_completed_cycle_ordinal << '\n';
    }
    expect(point.sampling.trailing_complete_cycle_count == 32U &&
               sample.completed_cycle_count == 32U &&
               sample.first_completed_cycle_ordinal ==
                   expected.first_completed_cycle_ordinal &&
               sample.last_completed_cycle_ordinal ==
                   expected.last_completed_cycle_ordinal &&
               point.sampling.last_eligible_completed_cycle_ordinal_at_fixed_horizon ==
                   expected.last_completed_cycle_ordinal &&
               block.completed_cycles.size() == 32U &&
               std::isfinite(block.indicated_gas_work_j) &&
               std::isfinite(block.aggregate_loss_work_j) &&
               std::isfinite(block.starter_work_j) &&
               std::isfinite(block.brake_work_j) &&
               complete_finite_torque(block.cycle_mean_torque.indicated_gas) &&
               complete_finite_torque(block.cycle_mean_torque.aggregate_loss) &&
               complete_finite_torque(block.cycle_mean_torque.starter) &&
               complete_finite_torque(block.cycle_mean_torque.net_shaft) &&
               std::isfinite(block.net_bmep_pa) && std::isfinite(block.mean_power_w),
           "held-regression run retained a different or incomplete cycle sample");
}

void test_all_fixed_horizon_samples(
    const profiles::BmwM52b28HeldRegressionRequestSet &request_set) {
    for (std::size_t index = 0; index < request_set.size(); ++index) {
        test_fixed_horizon_sample(request_set[index], kExpectedPoints[index]);
    }
}

void run_tests() {
    const auto request_set = make_request_set();
    test_exact_set_shape(request_set);
    test_identities(request_set);
    test_mutation_and_reorder_rejection(request_set);
    test_all_fixed_horizon_samples(request_set);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "BMW M52B28 held-regression request-set test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
