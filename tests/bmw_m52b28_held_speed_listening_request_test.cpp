#include "engine_sim_offline/profiles/bmw_m52b28_held_speed_listening_request.hpp"
#include "engine_sim_offline/request_identity.hpp"

#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline;

constexpr std::string_view kExpectedRequestIdentitySha256 =
    "7b503e180d3e3c08691243875cf18ea107a43d71f126163d7732b550c4ee06ba";
constexpr std::string_view kExpectedProvenanceSha256 =
    "fbd4f4cbfbe95e31d20d636cf219b4c776b0be0d49eda647e78707c27adc85bd";

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

[[nodiscard]] profiles::BmwM52b28HeldSpeedListeningRequest make_request() {
    auto result = profiles::make_bmw_m52b28_held_speed_listening_request();
    const auto *request =
        std::get_if<profiles::BmwM52b28HeldSpeedListeningRequest>(&result);
    if (request == nullptr) {
        const auto &report = std::get<contract::ValidationReport>(result);
        std::string message = "canonical BMW held-speed request was rejected";
        for (const auto &issue : report.issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{std::move(message)};
    }
    return *request;
}

[[nodiscard]] identity::SimulationRequestIdentityEncoding
request_identity(const profiles::BmwM52b28HeldSpeedListeningRequest &request) {
    auto result = identity::encode_simulation_request_identity_v2(
        request.engine, request.scenario, request.provenance.bundle);
    const auto *encoding =
        std::get_if<identity::SimulationRequestIdentityEncoding>(&result);
    expect(encoding != nullptr,
           "canonical BMW held-speed request identity did not encode");
    return *encoding;
}

void test_exact_request_shape(
    const profiles::BmwM52b28HeldSpeedListeningRequest &request) {
    expect(profiles::validate_bmw_m52b28_held_speed_listening_request(request).ok(),
           "canonical BMW held-speed request failed exact revalidation");
    expect(request.engine.profile_id.value ==
                   "bmw-m52b28-low-order-operating-point-v1" &&
               request.scenario.scenario_id == "bmw-m52b28-held-3000rpm-listening-v1" &&
               request.scenario.engine_profile_id == request.engine.profile_id.value,
           "canonical held-speed engine/scenario identity changed");

    const auto *held = std::get_if<contract::HeldSpeed>(&request.scenario.mode);
    const auto *preparation =
        std::get_if<contract::ConvergenceSettling>(&request.scenario.preparation);
    expect(held != nullptr && preparation != nullptr,
           "canonical request lost held-speed convergence mode");
    expect(std::bit_cast<std::uint64_t>(held->engine_speed_rpm.value) ==
                   std::bit_cast<std::uint64_t>(3000.0) &&
               std::bit_cast<std::uint64_t>(held->throttle_01.value) ==
                   std::bit_cast<std::uint64_t>(0.85) &&
               preparation->comparison_cycle_count.value == 16U &&
               std::bit_cast<std::uint64_t>(
                   preparation->maximum_preparation_duration_s.value) ==
                   std::bit_cast<std::uint64_t>(3.22) &&
               std::bit_cast<std::uint64_t>(
                   preparation->cycle_mean_torque_tolerance_nm.value) ==
                   std::bit_cast<std::uint64_t>(0.25) &&
               std::bit_cast<std::uint64_t>(preparation->pressure_tolerance_pa.value) ==
                   std::bit_cast<std::uint64_t>(1500.0),
           "canonical held point or calibrated convergence policy changed");

    expect(request.scenario.operating_state.value ==
               std::vector<contract::OperatingStatePoint>{
                   {"held-running", 0.0, {true, true, false, true, false}},
               },
           "canonical held-running control state changed");
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
                                          request.scenario.rates.physics) == 32200U &&
            contract::resolve_frame_index(request.scenario.audible_duration_s.value,
                                          request.scenario.rates.source_processing) ==
                2880000U &&
            contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                          request.scenario.rates.capture) == 182200U &&
            contract::resolve_frame_index(request.scenario.total_duration_s.value,
                                          request.scenario.rates.delivery) == 3498240U,
        "canonical held-speed horizons or clocks changed");
    expect(request.scenario.quality.value.capture_block_capacity_frames == 200U &&
               request.scenario.quality.value.event_journal_capacity_records == 3800U &&
               request.scenario.public_seed.value == UINT64_C(0xC0FFEE),
           "canonical held-speed transport or seed changed");

    expect(std::ranges::all_of(
               request.provenance.resolutions,
               [](const auto &resolution) {
                   return resolution.id.starts_with("bmw-m52b28-operating-profile-"
                                                    "resolution-") &&
                          resolution.parameter_path.find("legacy-low-order-v1") ==
                              std::string::npos;
               }),
           "held-speed request reused an M3 resolution identity or root");
}

void test_identity_and_mutation_rejection(
    const profiles::BmwM52b28HeldSpeedListeningRequest &request) {
    const auto identity = request_identity(request);
    expect(identity.sha256 == contract::sha256(identity.bytes),
           "held-speed request identity digest did not cover its bytes");
    expect(digest_hex(identity.sha256) == kExpectedRequestIdentitySha256,
           "held-speed request identity changed");
    expect(digest_hex(request.provenance.bundle.sha256) == kExpectedProvenanceSha256,
           "held-speed request provenance identity changed");

    auto changed = request;
    std::get<contract::HeldSpeed>(changed.scenario.mode).throttle_01.value = 0.84;
    expect(!profiles::validate_bmw_m52b28_held_speed_listening_request(changed).ok(),
           "mutated held-speed throttle passed exact validation");

    changed = request;
    std::get<contract::ConvergenceSettling>(changed.scenario.preparation)
        .pressure_tolerance_pa.value = 1501.0;
    expect(!profiles::validate_bmw_m52b28_held_speed_listening_request(changed).ok(),
           "mutated convergence calibration passed exact validation");
}

void test_calibrated_request_converges(
    const profiles::BmwM52b28HeldSpeedListeningRequest &request) {
    const auto identity = request_identity(request);
    auto plan_result =
        simulation::compile_low_order_capture_plan(request.engine, request.scenario);
    const auto *plan = std::get_if<simulation::LowOrderCapturePlan>(&plan_result);
    expect(plan != nullptr, "canonical held-speed capture plan did not compile");

    auto operating_result = simulation::compile_low_order_operating_point_v1_runtime(
        request.engine, request.scenario, *plan, identity.sha256);
    auto *operating =
        std::get_if<simulation::LowOrderOperatingPointV1Runtime>(&operating_result);
    expect(operating != nullptr, "canonical held-speed runtime did not compile");

    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        request.engine.physics_profile);
    auto core_result = simulation::compile_low_order_engine_core_v1_runtime(
        request.engine, request.scenario, profile.core);
    auto *core = std::get_if<simulation::LowOrderEngineCoreV1Runtime>(&core_result);
    expect(core != nullptr, "canonical held-speed engine core did not compile");

    while (operating->accepted_sample_count() < operating->fixed_cutoff_frame_count()) {
        auto step_result = core->advance();
        const auto *step =
            std::get_if<simulation::LowOrderEngineCoreV1StepView>(&step_result);
        expect(step != nullptr,
               "held-speed engine core ended before preparation cutoff");
        const auto advance = operating->advance(step->mechanics.get(), step->gas.get());
        if (const auto *failure = std::get_if<contract::FailureContext>(&advance)) {
            throw std::runtime_error{"calibrated held-speed request failed: " +
                                     failure->state_summary};
        }
    }

    expect(operating->finalized() && !operating->faulted() &&
               operating->operating_point_result().has_value(),
           "calibrated held-speed request did not publish an operating point");
    const auto &point = *operating->operating_point_result();
    expect(contract::validate(point, request.scenario, request.engine, identity.sha256)
               .ok(),
           "calibrated result failed request-bound validation");
    expect(point.convergence.block_a.cycles.completed_cycle_count == 16U &&
               point.convergence.block_a.cycles.first_completed_cycle_ordinal == 47U &&
               point.convergence.block_a.cycles.last_completed_cycle_ordinal == 62U &&
               point.convergence.block_b.cycles.completed_cycle_count == 16U &&
               point.convergence.block_b.cycles.first_completed_cycle_ordinal == 63U &&
               point.convergence.block_b.cycles.last_completed_cycle_ordinal == 78U,
           "calibrated request retained a different adjacent-cycle window");
    expect(std::bit_cast<std::uint64_t>(point.convergence.torque_residual_nm) ==
                   std::bit_cast<std::uint64_t>(0.14219052207965888) &&
               std::bit_cast<std::uint64_t>(point.convergence.pressure_residual_pa) ==
                   std::bit_cast<std::uint64_t>(870.20266385539435),
           "calibrated request residuals differ from the documented observation");
}

void run_tests() {
    const auto request = make_request();
    test_exact_request_shape(request);
    test_identity_and_mutation_rejection(request);
    test_calibrated_request_converges(request);
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "BMW M52B28 held-speed listening request test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
