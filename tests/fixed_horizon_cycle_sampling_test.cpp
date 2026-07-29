#include "simulation/fixed_horizon_cycle_sampling.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::simulation;

constexpr double kCycleRadians = 4.0 * std::numbers::pi_v<double>;

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

[[nodiscard]] contract::Sha256Digest descriptor_digest(std::string_view descriptor) {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

[[nodiscard]] FixedHorizonCycleSamplerPlan
plan(std::uint32_t cycle_count = 3U, double horizon_s = 5.0,
     std::vector<contract::GasVolumeId> volume_ids = {
         contract::GasVolumeId{2},
         contract::GasVolumeId{7},
         contract::GasVolumeId{11},
     }) {
    return {
        fixed_horizon_cycle_sampling_method_identity(),
        cycle_count,
        horizon_s,
        std::move(volume_ids),
    };
}

[[nodiscard]] FixedHorizonCycleSampler sampler(FixedHorizonCycleSamplerPlan value) {
    auto compiled = compile_fixed_horizon_cycle_sampler(std::move(value));
    const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&compiled);
    expect(error == nullptr, "valid fixed-horizon sampling plan was rejected");
    return std::get<FixedHorizonCycleSampler>(std::move(compiled));
}

[[nodiscard]] FixedHorizonCycle
cycle_with_work_lanes(std::uint64_t ordinal, double start_time_s, double end_time_s,
                      double indicated_gas_work_j,
                      double positive_aggregate_loss_work_j, double starter_work_j,
                      std::vector<double> pressures,
                      std::vector<contract::GasVolumeId> volume_ids = {
                          contract::GasVolumeId{2},
                          contract::GasVolumeId{7},
                          contract::GasVolumeId{11},
                      }) {
    expect(pressures.size() == volume_ids.size(),
           "test cycle pressure shape is invalid");
    std::vector<FixedHorizonCyclePressure> pressure_state;
    pressure_state.reserve(pressures.size());
    for (std::size_t index = 0; index < pressures.size(); ++index) {
        pressure_state.push_back({volume_ids[index], pressures[index]});
    }

    const auto start_sample = ordinal * 10U;
    const auto end_sample = (ordinal + 1U) * 10U;
    const double brake_work_j =
        (indicated_gas_work_j - positive_aggregate_loss_work_j) + starter_work_j;
    return {
        ordinal,
        {
            {start_sample, start_sample, 0.0},
            static_cast<double>(ordinal) * kCycleRadians,
            start_time_s,
        },
        {
            {end_sample, end_sample, 0.0},
            static_cast<double>(ordinal + 1U) * kCycleRadians,
            end_time_s,
        },
        indicated_gas_work_j,
        positive_aggregate_loss_work_j,
        starter_work_j,
        brake_work_j,
        std::move(pressure_state),
    };
}

[[nodiscard]] FixedHorizonCycle cycle(std::uint64_t ordinal, double start_time_s,
                                      double end_time_s, double brake_work_j,
                                      std::vector<double> pressures,
                                      std::vector<contract::GasVolumeId> volume_ids = {
                                          contract::GasVolumeId{2},
                                          contract::GasVolumeId{7},
                                          contract::GasVolumeId{11},
                                      }) {
    double indicated_gas_work_j = 0.0;
    double positive_aggregate_loss_work_j = 1.0;
    if (std::isfinite(brake_work_j) && brake_work_j > 0.0) {
        positive_aggregate_loss_work_j = brake_work_j;
        indicated_gas_work_j = 2.0 * brake_work_j;
    } else if (std::isfinite(brake_work_j) && brake_work_j < 0.0) {
        positive_aggregate_loss_work_j = -brake_work_j;
    } else if (brake_work_j == 0.0) {
        indicated_gas_work_j = positive_aggregate_loss_work_j;
    } else {
        indicated_gas_work_j = brake_work_j;
    }
    auto result =
        cycle_with_work_lanes(ordinal, start_time_s, end_time_s, indicated_gas_work_j,
                              positive_aggregate_loss_work_j, 0.0, std::move(pressures),
                              std::move(volume_ids));
    if (std::isfinite(brake_work_j)) {
        expect(std::bit_cast<std::uint64_t>(result.brake_work_j) ==
                   std::bit_cast<std::uint64_t>(brake_work_j),
               "test helper did not preserve requested brake work exactly");
    }
    return result;
}

void expect_accepted(FixedHorizonCycleSampler &value, const FixedHorizonCycle &input,
                     bool expected_eligible) {
    const auto result = value.observe(input);
    const auto *accepted = std::get_if<FixedHorizonCycleObservationAccepted>(&result);
    expect(accepted != nullptr && accepted->eligible == expected_eligible,
           "valid cycle observation was rejected or misclassified");
    expect(!value.finalized(),
           "observation published a result before fixed-horizon finalization");
}

void test_method_identity_and_plan_admission() {
    constexpr std::string_view kExpectedDigest =
        "9efbb15d0ad27d3f97d75d135b642c9a7feec6610c50e7ec82523ec62808da63";
    const auto descriptor = fixed_horizon_cycle_sampling_method_descriptor();
    expect(!descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos,
           "fixed-horizon descriptor is not canonical LF text");
    const auto digest = descriptor_digest(descriptor);
    expect(digest_hex(digest) == kExpectedDigest,
           "fixed-horizon descriptor digest changed");

    const auto &identity = fixed_horizon_cycle_sampling_method_identity();
    expect(identity.id == "fixed-horizon-trailing-complete-cycle-sample-v1" &&
               identity.version == 1U && identity.configuration_sha256 == digest &&
               contract::validate(identity).ok() &&
               &identity == &fixed_horizon_cycle_sampling_method_identity() &&
               &identity == &contract::fixed_horizon_cycle_sampling_method_identity(),
           "fixed-horizon method identity is invalid or unstable");

    {
        auto value = plan();
        value.method.id += "-mutation";
        const auto compiled = compile_fixed_horizon_cycle_sampler(std::move(value));
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&compiled);
        expect(error != nullptr &&
                   error->plan_issue ==
                       FixedHorizonCycleSamplingPlanIssue::unsupported_method_identity,
               "mutated sampling method was admitted");
    }
    {
        const auto compiled = compile_fixed_horizon_cycle_sampler(plan(0U));
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&compiled);
        expect(error != nullptr &&
                   error->plan_issue ==
                       FixedHorizonCycleSamplingPlanIssue::invalid_cycle_count,
               "zero trailing cycle count was admitted");
    }
    {
        const auto compiled = compile_fixed_horizon_cycle_sampler(plan(3U, 0.0));
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&compiled);
        expect(error != nullptr &&
                   error->plan_issue ==
                       FixedHorizonCycleSamplingPlanIssue::invalid_fixed_horizon,
               "nonpositive fixed horizon was admitted");
    }
    {
        const auto compiled = compile_fixed_horizon_cycle_sampler(
            plan(3U, 5.0, {contract::GasVolumeId{2}, contract::GasVolumeId{2}}));
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&compiled);
        expect(error != nullptr && error->plan_issue ==
                                       FixedHorizonCycleSamplingPlanIssue::
                                           unstable_pressure_identity_order,
               "duplicate pressure identities were admitted");
    }
}

void test_latest_sample_exact_reductions_and_stable_terminal_result() {
    auto value = sampler(plan(3U, 5.0));
    const std::array brake_works{
        10.0 * kCycleRadians, 11.0 * kCycleRadians, 12.0 * kCycleRadians,
        13.0 * kCycleRadians, 14.0 * kCycleRadians,
    };
    const std::array pressures{
        std::array{100.0, 200.0, 300.0}, std::array{101.0, 201.0, 301.0},
        std::array{102.0, 202.0, 302.0}, std::array{103.0, 203.0, 303.0},
        std::array{104.0, 204.0, 304.0},
    };
    for (std::size_t index = 0; index < brake_works.size(); ++index) {
        expect_accepted(
            value,
            cycle(index, static_cast<double>(index), static_cast<double>(index + 1U),
                  brake_works[index],
                  {pressures[index][0], pressures[index][1], pressures[index][2]}),
            true);
    }
    expect(value.retained_cycle_capacity() == 3U &&
               value.retained_eligible_cycle_count() == 3U,
           "sampler did not retain exactly the latest M complete cycles");

    const auto finalized = value.finalize_at_fixed_horizon();
    const auto *sampled = std::get_if<FixedHorizonCycleSampled>(&finalized);
    expect(sampled != nullptr, "valid fixed-horizon sample did not finalize");
    const auto &evidence = sampled->evidence;
    const auto &sample = evidence.trailing_complete_cycles;
    expect(evidence.method == fixed_horizon_cycle_sampling_method_identity() &&
               evidence.trailing_complete_cycle_count == 3U &&
               evidence.fixed_preparation_horizon_s == 5.0 &&
               sample.range.first_cycle_ordinal == 2U &&
               sample.range.last_cycle_ordinal == 4U &&
               sample.range.start_boundary.time_s == 2.0 &&
               sample.range.end_boundary.time_s == 5.0 &&
               evidence.last_eligible_completed_cycle_ordinal_at_fixed_horizon == 4U &&
               evidence.last_eligible_cycle_end_boundary_at_fixed_horizon ==
                   sample.range.end_boundary,
           "fixed sample range or last-eligible attestation changed");
    expect(sample.completed_cycles.size() == 3U,
           "fixed sample omitted per-cycle evidence");

    const double expected_brake = brake_works[2] + brake_works[3] + brake_works[4];
    const double expected_loss = expected_brake;
    const double expected_indicated =
        2.0 * brake_works[2] + 2.0 * brake_works[3] + 2.0 * brake_works[4];
    const double expected_torque =
        expected_brake / (static_cast<double>(3U) * 4.0 * std::numbers::pi_v<double>);
    expect(
        std::bit_cast<std::uint64_t>(sample.total_indicated_gas_work_j) ==
                std::bit_cast<std::uint64_t>(expected_indicated) &&
            std::bit_cast<std::uint64_t>(sample.total_positive_aggregate_loss_work_j) ==
                std::bit_cast<std::uint64_t>(expected_loss) &&
            std::bit_cast<std::uint64_t>(sample.total_starter_work_j) ==
                std::bit_cast<std::uint64_t>(0.0) &&
            std::bit_cast<std::uint64_t>(sample.total_brake_work_j) ==
                std::bit_cast<std::uint64_t>(expected_brake) &&
            std::bit_cast<std::uint64_t>(sample.mean_brake_torque_nm) ==
                std::bit_cast<std::uint64_t>(expected_torque),
        "fixed sample changed chronological work or torque reduction");

    const std::array expected_means{103.0, 203.0, 303.0};
    expect(sample.mean_boundary_pressures.size() == expected_means.size(),
           "fixed sample pressure mean shape changed");
    for (std::size_t index = 0; index < expected_means.size(); ++index) {
        expect(sample.mean_boundary_pressures[index].gas_volume_id ==
                       std::array{contract::GasVolumeId{2}, contract::GasVolumeId{7},
                                  contract::GasVolumeId{11}}[index] &&
                   sample.mean_boundary_pressures[index].mean_pressure_pa_abs ==
                       expected_means[index],
               "fixed sample pressure identity, order, or mean changed");
    }
    for (std::size_t index = 0; index < sample.completed_cycles.size(); ++index) {
        expect(sample.completed_cycles[index].completed_cycle_ordinal == 2U + index &&
                   sample.completed_cycles[index].end_boundary_pressures.size() == 3U,
               "fixed sample per-cycle chronology or pressure evidence changed");
    }

    expect(value.finalize_at_fixed_horizon() == finalized,
           "successful fixed-horizon finalization is not stable");
    expect(std::holds_alternative<FixedHorizonCycleObservationClosed>(value.observe(
               cycle(5U, 5.0, 6.0, kCycleRadians, {100.0, 200.0, 300.0}))),
           "finalized sampler accepted another cycle");
}

void test_chronological_pressure_reduction() {
    auto value = sampler(plan(3U, 3.0, {contract::GasVolumeId{2}}));
    constexpr double kLarge = 9007199254740992.0;
    constexpr std::array pressures{kLarge, 1.0, 1.0};
    for (std::size_t index = 0; index < pressures.size(); ++index) {
        expect_accepted(value,
                        cycle(index, static_cast<double>(index),
                              static_cast<double>(index + 1U), kCycleRadians,
                              {pressures[index]}, {contract::GasVolumeId{2}}),
                        true);
    }
    const auto result = value.finalize_at_fixed_horizon();
    const auto *sampled = std::get_if<FixedHorizonCycleSampled>(&result);
    expect(sampled != nullptr &&
               std::bit_cast<std::uint64_t>(
                   sampled->evidence.trailing_complete_cycles.mean_boundary_pressures
                       .front()
                       .mean_pressure_pa_abs) == UINT64_C(0x4325555555555555),
           "pressure mean was not reduced left-to-right in chronology");
}

void test_insufficient_and_malformed_inputs_are_terminal() {
    {
        auto value = sampler(plan(3U, 3.0));
        expect_accepted(
            value, cycle(0U, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0}), true);
        expect_accepted(
            value, cycle(1U, 1.0, 2.0, kCycleRadians, {101.0, 201.0, 301.0}), true);
        const auto first = value.finalize_at_fixed_horizon();
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&first);
        expect(error != nullptr &&
                   error->code ==
                       FixedHorizonCycleSamplingErrorCode::insufficient_cycles &&
                   error->retained_cycle_count == 2U &&
                   error->required_cycle_count == 3U &&
                   value.finalize_at_fixed_horizon() == first,
               "insufficient sample did not produce one stable typed error");
    }
    {
        auto value = sampler(plan());
        auto input = cycle(0U, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0});
        input.end_boundary_pressures.pop_back();
        const auto first = value.observe(input);
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&first);
        expect(error != nullptr &&
                   error->code ==
                       FixedHorizonCycleSamplingErrorCode::malformed_cycle_input &&
                   error->input_issue ==
                       FixedHorizonCycleSamplingInputIssue::pressure_shape_mismatch &&
                   value.observe(cycle(0U, 0.0, 1.0, kCycleRadians,
                                       {100.0, 200.0, 300.0})) == first &&
                   std::get<FixedHorizonCycleSamplingError>(
                       value.finalize_at_fixed_horizon()) == *error,
               "malformed input did not retain its first stable typed error");
    }
    {
        auto value = sampler(plan());
        auto input = cycle(0U, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0});
        input.starter_work_j = -0.0;
        const auto result = value.observe(input);
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result);
        expect(error != nullptr &&
                   error->input_issue ==
                       FixedHorizonCycleSamplingInputIssue::noncanonical_starter_work,
               "negative-zero starter work was admitted");
    }
    {
        auto value = sampler(plan());
        auto input = cycle(0U, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0});
        input.brake_work_j =
            std::nextafter(input.brake_work_j, std::numeric_limits<double>::infinity());
        const auto result = value.observe(input);
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result);
        expect(error != nullptr &&
                   error->input_issue ==
                       FixedHorizonCycleSamplingInputIssue::incoherent_brake_work,
               "incoherent brake work was admitted");
    }
    {
        auto value = sampler(plan());
        expect_accepted(
            value, cycle(0U, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0}), true);
        const auto result =
            value.observe(cycle(2U, 1.0, 2.0, kCycleRadians, {101.0, 201.0, 301.0}));
        const auto *error = std::get_if<FixedHorizonCycleSamplingError>(&result);
        expect(error != nullptr &&
                   error->input_issue ==
                       FixedHorizonCycleSamplingInputIssue::noncontiguous_cycle_ordinal,
               "noncontiguous cycle ordinal was admitted");
    }
}

void run_tests() {
    test_method_identity_and_plan_admission();
    test_latest_sample_exact_reductions_and_stable_terminal_result();
    test_chronological_pressure_reduction();
    test_insufficient_and_malformed_inputs_are_terminal();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "fixed-horizon cycle sampling test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
