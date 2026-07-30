#include "engine_sim_offline/profiles/bmw_m52b28_held_idle_low_load_request.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include "profiles/bmw_m52b28_profile_internal.hpp"
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

constexpr std::array<ExpectedPoint, profiles::kBmwM52b28HeldIdleLowLoadPointCount>
    kExpectedPoints{{
        {
            "rpm700-throttle0",
            "bmw-m52b28-held-idle-region-rpm700-throttle0",
            700.0,
            0.0,
            UINT64_C(42),
            UINT64_C(73),
            "d8a84c969cdea38ccb956b89394ca0d0177386304797f3ebc12ce444034ac345",
            "93939516151549f2af2c2c0eb586a10e6b5e039055db64e2783dc15deef3781d",
        },
        {
            "rpm1500-throttle0p10",
            "bmw-m52b28-held-low-load-rpm1500-throttle0p10",
            1500.0,
            0.10,
            UINT64_C(127),
            UINT64_C(158),
            "0241eab5c635a07d816ee53a393672a92ff6130a6af3024e1621872bb69541fa",
            "93939516151549f2af2c2c0eb586a10e6b5e039055db64e2783dc15deef3781d",
        },
    }};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string report_text(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        result += "\n  " + issue.path + ": " + issue.message;
    }
    return result;
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

[[nodiscard]] profiles::BmwM52b28HeldIdleLowLoadRequestSet make_request_set() {
    auto result = profiles::make_bmw_m52b28_held_idle_low_load_request_set();
    const auto *request_set =
        std::get_if<profiles::BmwM52b28HeldIdleLowLoadRequestSet>(&result);
    if (request_set == nullptr) {
        throw std::runtime_error{
            "canonical BMW held idle/low-load request set was rejected" +
            report_text(std::get<contract::ValidationReport>(result))};
    }
    return *request_set;
}

[[nodiscard]] identity::SimulationRequestIdentityEncoding
request_identity(const profiles::BmwM52b28HeldIdleLowLoadRequest &request) {
    auto random_plan_result =
        profiles::detail::compile_bmw_m52b28_migration_oracle_random_plan(
            request.engine, request.scenario);
    const auto *random_plan =
        std::get_if<contract::RandomPlan>(&random_plan_result);
    expect(random_plan != nullptr,
           "canonical BMW held idle/low-load random plan did not compile");
    auto result = identity::encode_simulation_request_identity_v3(
        request.engine, request.scenario, *random_plan, request.provenance.bundle);
    const auto *encoding =
        std::get_if<identity::SimulationRequestIdentityEncoding>(&result);
    expect(encoding != nullptr,
           "canonical BMW held idle/low-load request identity did not encode");
    return *encoding;
}

void test_exact_set_shape(
    const profiles::BmwM52b28HeldIdleLowLoadRequestSet &request_set) {
    expect(request_set.size() == profiles::kBmwM52b28HeldIdleLowLoadPointCount &&
               request_set.size() == kExpectedPoints.size(),
           "canonical held idle/low-load set extent changed");
    expect(
        profiles::validate_bmw_m52b28_held_idle_low_load_request_set(request_set).ok(),
        "canonical held idle/low-load set failed exact revalidation");

    const auto &common = request_set.front();
    for (std::size_t index = 0; index < request_set.size(); ++index) {
        const auto &request = request_set[index];
        const auto &expected = kExpectedPoints[index];
        expect(request.point_key == expected.point_key &&
                   request.scenario.scenario_id == expected.scenario_id &&
                   request.engine.profile_id.value ==
                       "bmw-m52b28-low-order-operating-point-v1" &&
                   request.scenario.engine_profile_id ==
                       request.engine.profile_id.value,
               "held idle/low-load point identity or order changed");

        const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
        const auto *sampling = std::get_if<contract::FixedHorizonCycleSampling>(
            &request.scenario.preparation);
        expect(held != nullptr && sampling != nullptr,
               "held idle/low-load point lost its held fixed-horizon mode");
        expect(std::bit_cast<std::uint64_t>(held->engine_speed_rpm.value) ==
                       std::bit_cast<std::uint64_t>(expected.engine_speed_rpm) &&
                   std::bit_cast<std::uint64_t>(held->throttle_01.value) ==
                       std::bit_cast<std::uint64_t>(expected.throttle_01) &&
                   std::bit_cast<std::uint64_t>(
                       sampling->fixed_preparation_horizon_s.value) ==
                       std::bit_cast<std::uint64_t>(12.88) &&
                   sampling->trailing_complete_cycle_count.value == 32U &&
                   sampling->method.value ==
                       contract::fixed_horizon_cycle_sampling_method_identity(),
               "held idle/low-load operating point or sample policy changed");

        expect(request.scenario.operating_state.value ==
                   std::vector<contract::OperatingStatePoint>{
                       {"held-idle-low-load-running",
                        0.0,
                        {true, true, false, true, false}},
                   },
               "held idle/low-load running state changed");
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
                    std::bit_cast<std::uint64_t>(12.88) &&
                std::bit_cast<std::uint64_t>(
                    request.scenario.audible_duration_s.value) ==
                    std::bit_cast<std::uint64_t>(15.0) &&
                std::bit_cast<std::uint64_t>(request.scenario.total_duration_s.value) ==
                    std::bit_cast<std::uint64_t>(12.88 + 15.0),
            "held idle/low-load timing or rates changed");
        expect(
            contract::resolve_frame_index(request.scenario.audible_start_s.value,
                                          request.scenario.rates.physics) == 128800U &&
                contract::resolve_frame_index(request.scenario.audible_start_s.value,
                                              request.scenario.rates.capture) ==
                    128800U &&
                contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                              request.scenario.rates.physics) ==
                    278800U &&
                contract::resolve_frame_index(request.scenario.audible_duration_s.value,
                                              request.scenario.rates.delivery) ==
                    2880000U &&
                contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                              request.scenario.rates.delivery) ==
                    5352960U,
            "held idle/low-load frame boundaries changed");
        expect(request.scenario.quality.value.profile_id ==
                       "low-order-operating-point-listening-v1" &&
                   request.scenario.quality.value.version == 1U &&
                   request.scenario.quality.value.capture_block_capacity_frames ==
                       200U &&
                   request.scenario.quality.value.event_journal_capacity_records ==
                       3800U &&
                   request.scenario.public_seed.value == UINT64_C(0xC0FFEE),
               "held idle/low-load quality, transport, or seed changed");

        expect(std::bit_cast<std::uint64_t>(
                   request.scenario.ambient.pressure_pa_abs.value) ==
                       std::bit_cast<std::uint64_t>(101325.0) &&
                   std::bit_cast<std::uint64_t>(
                       request.scenario.ambient.temperature_k.value) ==
                       std::bit_cast<std::uint64_t>(298.15) &&
                   std::bit_cast<std::uint64_t>(
                       request.scenario.ambient.relative_humidity_01.value) ==
                       std::bit_cast<std::uint64_t>(0.0) &&
                   request.engine == common.engine &&
                   request.scenario.ambient == common.scenario.ambient &&
                   request.scenario.initial_thermal_state ==
                       common.scenario.initial_thermal_state &&
                   request.scenario.crankcase == common.scenario.crankcase &&
                   request.scenario.fuel == common.scenario.fuel &&
                   request.provenance.bundle == common.provenance.bundle,
               "held idle/low-load points no longer share canonical conditions");

        const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
            request.engine.physics_profile);
        expect(profile.accessory_configuration.configuration_id.value ==
                       "bmw-m52b28-warm-stock-accessories-v1" &&
                   profile.starter.mechanically_disengaged.value,
               "held idle/low-load accessory or starter condition changed");

        expect(std::ranges::all_of(
                   request.provenance.resolutions,
                   [](const auto &resolution) {
                       return resolution.id.starts_with(
                                  "bmw-m52b28-operating-profile-resolution-") &&
                              resolution.parameter_path.find("legacy-low-order-v1") ==
                                  std::string::npos;
                   }),
               "held idle/low-load request reused an M3 resolution root");

        if (index != 0U) {
            expect(request.engine.cylinders.data() != common.engine.cylinders.data() &&
                       request.scenario.operating_state.value.data() !=
                           common.scenario.operating_state.value.data() &&
                       request.provenance.resolutions.data() !=
                           common.provenance.resolutions.data(),
                   "held idle/low-load points share owned request storage");
        }
    }
}

[[nodiscard]] bool complete_finite_torque(const contract::TorqueValueNm &torque) {
    return std::isfinite(torque.value_nm) &&
           torque.availability == contract::Availability::available &&
           torque.completeness == contract::Completeness::complete &&
           torque.omitted_terms == 0U;
}

[[nodiscard]] contract::HeldSpeedOperatingPointResult
run_fixed_horizon_sample(const profiles::BmwM52b28HeldIdleLowLoadRequest &request,
                         const ExpectedPoint &expected) {
    const auto identity = request_identity(request);
    auto plan_result =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
    const auto *plan = std::get_if<simulation::LowOrderCapturePlan>(&plan_result);
    expect(plan != nullptr, "held idle/low-load capture plan did not compile");

    auto operating_result = simulation::compile_low_order_operating_point_v1_runtime(
        request.engine, request.scenario, *plan, identity.sha256);
    auto *operating =
        std::get_if<simulation::LowOrderOperatingPointV1Runtime>(&operating_result);
    expect(operating != nullptr,
           "held idle/low-load operating runtime did not compile");

    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        request.engine.physics_profile);
    auto random_plan_result =
        profiles::detail::compile_bmw_m52b28_migration_oracle_random_plan(
            request.engine, request.scenario);
    const auto *random_plan = std::get_if<contract::RandomPlan>(&random_plan_result);
    expect(random_plan != nullptr, "held idle/low-load random plan did not compile");
    auto core_result = simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, profile.core, *random_plan);
    auto *core = std::get_if<simulation::LowOrderEngineCoreV1Runtime>(&core_result);
    expect(core != nullptr, "held idle/low-load engine core did not compile");

    while (operating->accepted_sample_count() <
           operating->fixed_preparation_horizon_frame_count()) {
        auto step_result = core->advance();
        const auto *step =
            std::get_if<simulation::LowOrderEngineCoreV1StepView>(&step_result);
        expect(step != nullptr,
               "held idle/low-load engine core ended before the fixed horizon");
        const auto advance = operating->advance(step->mechanics.get(), step->gas.get());
        if (const auto *failure = std::get_if<contract::FailureContext>(&advance)) {
            throw std::runtime_error{"held idle/low-load fixed-horizon run failed: " +
                                     failure->state_summary};
        }
    }

    expect(operating->finalized() && !operating->faulted() &&
               operating->operating_point_result().has_value(),
           "held idle/low-load run did not publish an operating point");
    const auto point = *operating->operating_point_result();
    expect(contract::validate(point, request.scenario, request.engine, identity.sha256)
               .ok(),
           "held idle/low-load result failed request-bound validation");

    const auto &sample = point.sampling.trailing_complete_cycles.cycles;
    const auto &block = point.reported_block();
    if (sample.first_completed_cycle_ordinal !=
            expected.first_completed_cycle_ordinal ||
        sample.last_completed_cycle_ordinal != expected.last_completed_cycle_ordinal) {
        std::cerr << expected.point_key
                  << " sample cycle range: " << sample.first_completed_cycle_ordinal
                  << ".." << sample.last_completed_cycle_ordinal << '\n';
    }
    expect(std::bit_cast<std::uint64_t>(point.sampling.fixed_preparation_horizon_s) ==
                   std::bit_cast<std::uint64_t>(12.88) &&
               point.sampling.trailing_complete_cycle_count == 32U &&
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
           "held idle/low-load run retained a different or incomplete sample");
    return point;
}

void test_fixed_horizon_samples(
    const profiles::BmwM52b28HeldIdleLowLoadRequestSet &request_set) {
    const auto idle_first =
        run_fixed_horizon_sample(request_set[0], kExpectedPoints[0]);
    const auto idle_second =
        run_fixed_horizon_sample(request_set[0], kExpectedPoints[0]);
    expect(idle_first == idle_second,
           "independent 700-rpm fixed-horizon runs were not deterministic");
    static_cast<void>(run_fixed_horizon_sample(request_set[1], kExpectedPoints[1]));
}

void test_identities(const profiles::BmwM52b28HeldIdleLowLoadRequestSet &request_set) {
    bool all_goldens_match = true;
    std::array<contract::Sha256Digest, profiles::kBmwM52b28HeldIdleLowLoadPointCount>
        request_digests{};

    for (std::size_t index = 0; index < request_set.size(); ++index) {
        const auto identity = request_identity(request_set[index]);
        expect(identity.sha256 == contract::sha256(identity.bytes),
               "held idle/low-load identity digest did not cover its bytes");
        request_digests[index] = identity.sha256;

        const auto request_sha256 = digest_hex(identity.sha256);
        const auto provenance_sha256 =
            digest_hex(request_set[index].provenance.bundle.sha256);
        if (request_sha256 != kExpectedPoints[index].request_identity_sha256 ||
            provenance_sha256 != kExpectedPoints[index].provenance_sha256) {
            std::cerr << "BMW held idle/low-load point " << index
                      << " request SHA-256: " << request_sha256
                      << "\nBMW held idle/low-load point " << index
                      << " provenance SHA-256: " << provenance_sha256 << '\n';
            all_goldens_match = false;
        }
    }

    expect(request_digests[0] != request_digests[1],
           "held idle/low-load points reused a request identity");
    expect(all_goldens_match,
           "held idle/low-load request or provenance identity changed");
}

void test_mutation_and_reorder_rejection(
    const profiles::BmwM52b28HeldIdleLowLoadRequestSet &request_set) {
    const auto rejected = [](const auto &changed) {
        return !profiles::validate_bmw_m52b28_held_idle_low_load_request_set(changed)
                    .ok();
    };

    for (std::size_t index = 0; index < request_set.size(); ++index) {
        auto changed = request_set;
        changed[index].point_key += "-mutated";
        expect(rejected(changed), "mutated point key passed exact validation");

        changed = request_set;
        changed[index].scenario.scenario_id += "-mutated";
        expect(rejected(changed), "mutated scenario ID passed exact validation");

        changed = request_set;
        auto &held = std::get<contract::HeldSpeed>(changed[index].scenario.mode);
        held.engine_speed_rpm.value = 800.0;
        expect(rejected(changed), "mutated held RPM passed exact validation");

        changed = request_set;
        std::get<contract::HeldSpeed>(changed[index].scenario.mode).throttle_01.value =
            0.5;
        expect(rejected(changed), "mutated held throttle passed exact validation");

        changed = request_set;
        std::get<contract::FixedHorizonCycleSampling>(
            changed[index].scenario.preparation)
            .fixed_preparation_horizon_s.value = 6.44;
        expect(rejected(changed), "mutated preparation horizon passed validation");

        changed = request_set;
        std::get<contract::FixedHorizonCycleSampling>(
            changed[index].scenario.preparation)
            .trailing_complete_cycle_count.value = 31U;
        expect(rejected(changed), "mutated cycle sample passed exact validation");

        changed = request_set;
        changed[index].scenario.operating_state.value.front().event_id += "-mutated";
        expect(rejected(changed), "mutated running-state ID passed validation");

        changed = request_set;
        changed[index].scenario.total_duration_s.value = 21.44;
        expect(rejected(changed), "mutated total duration passed exact validation");

        changed = request_set;
        changed[index].scenario.public_seed.value ^= UINT64_C(1);
        expect(rejected(changed), "mutated public seed passed exact validation");
    }

    auto reordered = request_set;
    std::swap(reordered[0], reordered[1]);
    expect(rejected(reordered), "reordered held idle/low-load set passed validation");

    auto duplicated = request_set;
    duplicated[1] = duplicated[0];
    expect(rejected(duplicated),
           "duplicated held idle/low-load point passed validation");
}

void run_tests() {
    const auto request_set = make_request_set();
    test_exact_set_shape(request_set);
    test_identities(request_set);
    test_mutation_and_reorder_rejection(request_set);
    test_fixed_horizon_samples(request_set);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "BMW M52B28 held idle/low-load request-set test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
