#include "simulation/adjacent_cycle_block_convergence.hpp"

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

[[nodiscard]] contract::Sha256Digest descriptor_digest(std::string_view descriptor) {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

[[nodiscard]] AdjacentCycleBlockConvergencePlan
plan(std::uint32_t cycles_per_block, double threshold_s, double cutoff_s,
     double torque_tolerance_nm, double pressure_tolerance_pa,
     std::vector<contract::GasVolumeId> volume_ids = {
         contract::GasVolumeId{2},
         contract::GasVolumeId{7},
         contract::GasVolumeId{11},
     }) {
    return {
        adjacent_cycle_block_mean_convergence_method_identity(),
        cycles_per_block,
        threshold_s,
        cutoff_s,
        torque_tolerance_nm,
        pressure_tolerance_pa,
        std::move(volume_ids),
    };
}

[[nodiscard]] AdjacentCycleBlockConvergenceObserver
observer(AdjacentCycleBlockConvergencePlan value) {
    auto compiled = compile_adjacent_cycle_block_convergence_observer(std::move(value));
    const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&compiled);
    expect(error == nullptr, "valid convergence plan was rejected");
    return std::get<AdjacentCycleBlockConvergenceObserver>(std::move(compiled));
}

[[nodiscard]] AdjacentCycleBlockCycle
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
    std::vector<AdjacentCycleBlockPressure> pressure_state;
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

[[nodiscard]] AdjacentCycleBlockCycle
cycle(std::uint64_t ordinal, double start_time_s, double end_time_s,
      double brake_work_j, std::vector<double> pressures,
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

void expect_accepted(AdjacentCycleBlockConvergenceObserver &value,
                     const AdjacentCycleBlockCycle &input, bool expected_eligible) {
    const auto result = value.observe(input);
    const auto *accepted = std::get_if<AdjacentCycleBlockObservationAccepted>(&result);
    expect(accepted != nullptr && accepted->eligible == expected_eligible,
           "valid cycle observation was rejected or misclassified");
    expect(!value.finalized(),
           "observe reported convergence before fixed-cutoff finalization");
}

void test_method_identity_and_plan_admission() {
    constexpr std::string_view kExpectedDigest =
        "b1a1ad37088ceb2a88dbb5db4bb385067fefa6b244b091deeebe5bf38acac406";
    const auto descriptor = adjacent_cycle_block_mean_convergence_method_descriptor();
    expect(!descriptor.empty() && descriptor.back() == '\n' &&
               descriptor.find('\r') == std::string_view::npos &&
               descriptor.find('\0') == std::string_view::npos,
           "convergence descriptor is not canonical LF text");
    const auto digest = descriptor_digest(descriptor);
    if (digest_hex(digest) != kExpectedDigest) {
        std::cerr << "adjacent cycle-block convergence descriptor SHA-256: "
                  << digest_hex(digest) << '\n';
    }
    expect(digest_hex(digest) == kExpectedDigest,
           "convergence descriptor digest changed");

    const auto &identity = adjacent_cycle_block_mean_convergence_method_identity();
    expect(identity.id == "adjacent-nonoverlapping-cycle-block-mean-v1" &&
               identity.version == 1U && identity.configuration_sha256 == digest &&
               contract::validate(identity).ok() &&
               &identity == &adjacent_cycle_block_mean_convergence_method_identity(),
           "convergence method identity is invalid or unstable");

    constexpr std::array forbidden_tokens{"m4", "bmw", "fixture", "profile_id"};
    for (const auto token : forbidden_tokens) {
        expect(identity.id.find(token) == std::string::npos &&
                   descriptor.find(token) == std::string_view::npos,
               "convergence method authority contains product-specific text");
    }

    {
        auto value = plan(1, 0.0, 2.0, 1.0, 1.0);
        value.method.id += "-mutation";
        const auto result =
            compile_adjacent_cycle_block_convergence_observer(std::move(value));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(
            error != nullptr &&
                error->code == AdjacentCycleBlockConvergenceErrorCode::invalid_plan &&
                error->plan_issue ==
                    AdjacentCycleBlockConvergencePlanIssue::unsupported_method_identity,
            "mutated convergence method ID was admitted");
    }
    {
        auto value = plan(1, 0.0, 2.0, 1.0, 1.0);
        ++value.method.version;
        const auto result =
            compile_adjacent_cycle_block_convergence_observer(std::move(value));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(
            error != nullptr &&
                error->plan_issue ==
                    AdjacentCycleBlockConvergencePlanIssue::unsupported_method_identity,
            "mutated convergence method version was admitted");
    }
    {
        auto value = plan(1, 0.0, 2.0, 1.0, 1.0);
        value.method.configuration_sha256.bytes.front() ^= UINT8_C(0x80);
        const auto result =
            compile_adjacent_cycle_block_convergence_observer(std::move(value));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(
            error != nullptr &&
                error->plan_issue ==
                    AdjacentCycleBlockConvergencePlanIssue::unsupported_method_identity,
            "mutated convergence method digest was admitted");
    }
    {
        const auto result = compile_adjacent_cycle_block_convergence_observer(
            plan(0, 0.0, 2.0, 1.0, 1.0));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr &&
                   error->plan_issue ==
                       AdjacentCycleBlockConvergencePlanIssue::invalid_cycle_count,
               "zero convergence block size was admitted");
    }
    {
        const auto result = compile_adjacent_cycle_block_convergence_observer(
            plan(std::numeric_limits<std::uint32_t>::max(), 0.0, 2.0, 1.0, 1.0));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr &&
                   error->code ==
                       AdjacentCycleBlockConvergenceErrorCode::capacity_overflow,
               "overflowing two-block capacity was admitted");
    }
    {
        const auto result = compile_adjacent_cycle_block_convergence_observer(
            plan(1, 0.0, 2.0, 1.0, 1.0,
                 {contract::GasVolumeId{2}, contract::GasVolumeId{2}}));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr && error->plan_issue ==
                                       AdjacentCycleBlockConvergencePlanIssue::
                                           unstable_pressure_identity_order,
               "duplicate pressure identities were admitted");
    }
}

void test_exact_window_math_and_inclusive_boundaries() {
    auto value = observer(plan(2, 10.0, 18.0, 2.0, 3.0));

    expect_accepted(value,
                    cycle(39, 8.0, 10.0, 99.0 * kCycleRadians, {999.0, 999.0, 999.0}),
                    false);
    const std::array brake_works{
        10.0 * kCycleRadians,
        14.0 * kCycleRadians,
        13.0 * kCycleRadians,
        15.0 * kCycleRadians,
    };
    const std::array loss_works{
        4.0 * kCycleRadians,
        4.0 * kCycleRadians,
        4.0 * kCycleRadians,
        4.0 * kCycleRadians,
    };
    const std::array indicated_works{
        14.0 * kCycleRadians,
        18.0 * kCycleRadians,
        17.0 * kCycleRadians,
        19.0 * kCycleRadians,
    };
    const std::array pressures{
        std::array{100.0, 200.0, 300.0},
        std::array{104.0, 198.0, 302.0},
        std::array{101.0, 203.0, 297.0},
        std::array{105.0, 201.0, 301.0},
    };
    for (std::size_t index = 0; index < brake_works.size(); ++index) {
        const auto ordinal = 40U + index;
        auto input = cycle_with_work_lanes(
            ordinal, 10.0 + 2.0 * static_cast<double>(index),
            12.0 + 2.0 * static_cast<double>(index), indicated_works[index],
            loss_works[index], 0.0,
            {pressures[index][0], pressures[index][1], pressures[index][2]});
        expect(std::bit_cast<std::uint64_t>(input.brake_work_j) ==
                   std::bit_cast<std::uint64_t>(brake_works[index]),
               "analytic cycle work identity changed");
        expect_accepted(value, input, true);
    }

    expect(value.retained_cycle_capacity() == 4U &&
               value.retained_eligible_cycle_count() == 4U,
           "convergence observer retained the wrong exact window");
    const auto finalized = value.finalize_at_fixed_cutoff();
    const auto *converged = std::get_if<AdjacentCycleBlockConverged>(&finalized);
    expect(converged != nullptr && converged->evidence.settled,
           "inclusive residual boundary did not converge");
    const auto &evidence = converged->evidence;
    expect(evidence.block_a.range.first_cycle_ordinal == 40U &&
               evidence.block_a.range.last_cycle_ordinal == 41U &&
               evidence.block_b.range.first_cycle_ordinal == 42U &&
               evidence.block_b.range.last_cycle_ordinal == 43U &&
               evidence.block_a.range.start_boundary.time_s == 10.0 &&
               evidence.block_a.range.end_boundary.time_s == 14.0 &&
               evidence.block_b.range.start_boundary.time_s == 14.0 &&
               evidence.block_b.range.end_boundary.time_s == 18.0,
           "adjacent block ranges or inclusive time eligibility changed");
    expect_near(evidence.block_a.mean_brake_torque_nm, 12.0, 1.0e-12,
                "block A torque mean changed");
    expect_near(evidence.block_b.mean_brake_torque_nm, 14.0, 1.0e-12,
                "block B torque mean changed");
    expect(evidence.block_a.total_indicated_gas_work_j ==
                   indicated_works[0] + indicated_works[1] &&
               evidence.block_a.total_positive_aggregate_loss_work_j ==
                   loss_works[0] + loss_works[1] &&
               std::bit_cast<std::uint64_t>(evidence.block_a.total_starter_work_j) ==
                   std::bit_cast<std::uint64_t>(0.0) &&
               evidence.block_a.total_brake_work_j == brake_works[0] + brake_works[1] &&
               evidence.block_b.total_indicated_gas_work_j ==
                   indicated_works[2] + indicated_works[3] &&
               evidence.block_b.total_positive_aggregate_loss_work_j ==
                   loss_works[2] + loss_works[3] &&
               std::bit_cast<std::uint64_t>(evidence.block_b.total_starter_work_j) ==
                   std::bit_cast<std::uint64_t>(0.0) &&
               evidence.block_b.total_brake_work_j == brake_works[2] + brake_works[3],
           "coherent A/B work-lane sums changed");
    expect_near(evidence.torque_residual_nm, 2.0, 1.0e-12,
                "inclusive torque residual changed");
    expect(evidence.pressure_means.size() == 3U,
           "pressure mean vector has the wrong shape");
    const std::array expected_a{102.0, 199.0, 301.0};
    const std::array expected_b{103.0, 202.0, 299.0};
    for (std::size_t index = 0; index < expected_a.size(); ++index) {
        expect(evidence.pressure_means[index].block_a_mean_pressure_pa_abs ==
                       expected_a[index] &&
                   evidence.pressure_means[index].block_b_mean_pressure_pa_abs ==
                       expected_b[index],
               "phase-aligned pressure mean changed");
    }
    expect(evidence.pressure_residual_pa == 3.0 &&
               evidence.limiting_gas_volume_id == contract::GasVolumeId{7},
           "inclusive pressure residual or limiting identity changed");
    expect(evidence.block_a.range.start_boundary.interpolation ==
                   CycleBoundaryEvidence{400, 400, 0.0} &&
               evidence.block_b.range.end_boundary.interpolation ==
                   CycleBoundaryEvidence{440, 440, 0.0},
           "block range did not retain exact supplied boundary evidence");

    const auto repeated = value.finalize_at_fixed_cutoff();
    expect(repeated == finalized, "successful fixed-cutoff finalization is not stable");
    expect(std::holds_alternative<AdjacentCycleBlockObservationClosed>(
               value.observe(cycle(44, 18.0, 20.0, 0.0, {100.0, 200.0, 300.0}))),
           "finalized observer accepted another cycle");
}

void test_latest_window_no_early_success_and_nonconvergence() {
    auto value = observer(plan(1, 0.0, 10.0, 1.0, 1.0, {contract::GasVolumeId{2}}));
    const std::array torques{10.0, 10.0, 20.0, 10.0};
    const std::array pressures{100.0, 100.0, 200.0, 100.0};
    for (std::size_t index = 0; index < torques.size(); ++index) {
        expect_accepted(value,
                        cycle(index, static_cast<double>(index),
                              static_cast<double>(index + 1U),
                              torques[index] * kCycleRadians, {pressures[index]},
                              {contract::GasVolumeId{2}}),
                        true);
    }
    expect(value.retained_eligible_cycle_count() == 2U,
           "latest-2N observer did not evict older eligible cycles");

    const auto finalized = value.finalize_at_fixed_cutoff();
    const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&finalized);
    expect(error != nullptr &&
               error->code == AdjacentCycleBlockConvergenceErrorCode::nonconverged &&
               error->evidence.has_value() && !error->evidence->settled &&
               error->evidence->block_a.range.first_cycle_ordinal == 2U &&
               error->evidence->block_b.range.first_cycle_ordinal == 3U,
           "fixed-cutoff evaluation did not use the latest 2N cycles");
    expect_near(error->evidence->block_a.mean_brake_torque_nm, 20.0, 1.0e-12,
                "latest block A torque changed");
    expect_near(error->evidence->block_b.mean_brake_torque_nm, 10.0, 1.0e-12,
                "latest block B torque changed");
    expect_near(error->evidence->torque_residual_nm, 10.0, 1.0e-12,
                "latest torque residual changed");
    expect(error->evidence->pressure_residual_pa == 100.0,
           "latest pressure residual changed");
    expect(value.finalize_at_fixed_cutoff() == finalized,
           "nonconverged terminal result is not stable");
}

void test_first_ascending_pressure_tie_and_inclusive_comparison() {
    auto value = observer(plan(1, 0.0, 2.0, 1.0, 5.0));
    expect_accepted(
        value, cycle(0, 0.0, 1.0, 10.0 * kCycleRadians, {100.0, 200.0, 300.0}), true);
    expect_accepted(
        value, cycle(1, 1.0, 2.0, 10.0 * kCycleRadians, {105.0, 195.0, 304.0}), true);
    const auto finalized = value.finalize_at_fixed_cutoff();
    const auto *converged = std::get_if<AdjacentCycleBlockConverged>(&finalized);
    expect(converged != nullptr && converged->evidence.pressure_residual_pa == 5.0 &&
               converged->evidence.limiting_gas_volume_id == contract::GasVolumeId{2},
           "pressure tie did not retain the first ascending identity");
}

void test_insufficient_eligible_cycles() {
    auto value = observer(plan(2, 2.0, 8.0, 1.0, 1.0, {contract::GasVolumeId{2}}));
    for (std::uint64_t ordinal = 0; ordinal < 4U; ++ordinal) {
        expect_accepted(value,
                        cycle(ordinal, static_cast<double>(ordinal * 2U),
                              static_cast<double>((ordinal + 1U) * 2U), kCycleRadians,
                              {100.0}, {contract::GasVolumeId{2}}),
                        ordinal != 0U);
    }
    const auto finalized = value.finalize_at_fixed_cutoff();
    const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&finalized);
    expect(error != nullptr &&
               error->code ==
                   AdjacentCycleBlockConvergenceErrorCode::insufficient_cycles &&
               error->retained_cycle_count == 3U && error->required_cycle_count == 4U &&
               !error->evidence.has_value(),
           "insufficient eligible-cycle window did not fail closed");
    expect(value.finalize_at_fixed_cutoff() == finalized,
           "insufficient-cycle failure is not terminal and stable");
}

void test_stable_chronological_pressure_reduction() {
    auto value = observer(plan(3, 0.0, 6.0, 1.0, 1.0, {contract::GasVolumeId{2}}));
    constexpr double kLarge = 9007199254740992.0;
    constexpr std::array pressures{kLarge, 1.0, 1.0, kLarge, 1.0, 1.0};
    for (std::size_t index = 0; index < pressures.size(); ++index) {
        expect_accepted(value,
                        cycle(index, static_cast<double>(index),
                              static_cast<double>(index + 1U), kCycleRadians,
                              {pressures[index]}, {contract::GasVolumeId{2}}),
                        true);
    }
    const auto finalized = value.finalize_at_fixed_cutoff();
    const auto *converged = std::get_if<AdjacentCycleBlockConverged>(&finalized);
    expect(converged != nullptr, "equal stable-reduction blocks did not converge");
    const auto &means = converged->evidence.pressure_means.front();
    expect(std::bit_cast<std::uint64_t>(means.block_a_mean_pressure_pa_abs) ==
                   UINT64_C(0x4325555555555555) &&
               std::bit_cast<std::uint64_t>(means.block_b_mean_pressure_pa_abs) ==
                   UINT64_C(0x4325555555555555),
           "pressure reduction was not stable left-to-right chronology");
}

void test_malformed_cycle_inputs_are_terminal() {
    {
        auto value = observer(plan(1, 0.0, 2.0, 1.0, 1.0));
        auto input = cycle(0, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0});
        input.end_boundary_pressures.pop_back();
        const auto result = value.observe(input);
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr &&
                   error->code ==
                       AdjacentCycleBlockConvergenceErrorCode::malformed_cycle_input &&
                   error->input_issue ==
                       AdjacentCycleBlockConvergenceInputIssue::pressure_shape_mismatch,
               "wrong pressure shape did not fault observation");
        expect(value.observe(
                   cycle(0, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0})) == result,
               "malformed observation failure is not stable");
        const auto finalized = value.finalize_at_fixed_cutoff();
        expect(std::get<AdjacentCycleBlockConvergenceError>(finalized) == *error,
               "finalization did not preserve malformed observation error");
    }
    {
        auto value = observer(plan(1, 0.0, 2.0, 1.0, 1.0));
        const auto result =
            value.observe(cycle(0, 0.0, 1.0, std::numeric_limits<double>::infinity(),
                                {100.0, 200.0, 300.0}));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr &&
                   error->input_issue ==
                       AdjacentCycleBlockConvergenceInputIssue::nonfinite_cycle_work,
               "nonfinite brake work did not fault observation");
    }
    {
        auto value = observer(plan(1, 0.0, 2.0, 1.0, 1.0));
        const auto result = value.observe(
            cycle(0, 0.0, 1.0, kCycleRadians,
                  {100.0, std::numeric_limits<double>::quiet_NaN(), 300.0}));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr &&
                   error->input_issue ==
                       AdjacentCycleBlockConvergenceInputIssue::nonfinite_pressure &&
                   error->element_index == 1U,
               "nonfinite pressure did not fault the exact element");
    }
    {
        auto value = observer(plan(1, 0.0, 2.0, 1.0, 1.0));
        expect_accepted(value, cycle(0, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0}),
                        true);
        const auto result =
            value.observe(cycle(2, 1.0, 2.0, kCycleRadians, {100.0, 200.0, 300.0}));
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr && error->input_issue ==
                                       AdjacentCycleBlockConvergenceInputIssue::
                                           noncontiguous_cycle_ordinal,
               "noncontiguous complete-cycle ordinal was admitted");
    }
    {
        auto value = observer(plan(1, 0.0, 2.0, 1.0, 1.0));
        expect_accepted(value, cycle(0, 0.0, 1.0, kCycleRadians, {100.0, 200.0, 300.0}),
                        true);
        auto input = cycle(1, 1.0, 2.0, kCycleRadians, {100.0, 200.0, 300.0});
        input.start_boundary.interpolation = CycleBoundaryEvidence{11, 11, 0.0};
        const auto result = value.observe(input);
        const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
        expect(error != nullptr && error->input_issue ==
                                       AdjacentCycleBlockConvergenceInputIssue::
                                           noncontiguous_cycle_boundary,
               "noncontiguous boundary evidence was admitted");
    }
}

void expect_work_mutation_rejected(
    AdjacentCycleBlockCycle input,
    AdjacentCycleBlockConvergenceInputIssue expected_issue, std::string_view message) {
    auto value = observer(plan(1, 0.0, 2.0, 1.0, 1.0));
    const auto result = value.observe(input);
    const auto *error = std::get_if<AdjacentCycleBlockConvergenceError>(&result);
    expect(error != nullptr &&
               error->code ==
                   AdjacentCycleBlockConvergenceErrorCode::malformed_cycle_input &&
               error->input_issue == expected_issue,
           message);
}

void test_work_lane_mutations_fail_closed() {
    const auto valid = cycle(0, 0.0, 1.0, 10.0 * kCycleRadians, {100.0, 200.0, 300.0});

    {
        auto input = valid;
        input.indicated_gas_work_j = std::numeric_limits<double>::infinity();
        expect_work_mutation_rejected(
            std::move(input),
            AdjacentCycleBlockConvergenceInputIssue::nonfinite_cycle_work,
            "nonfinite indicated work was admitted");
    }
    {
        auto input = valid;
        input.positive_aggregate_loss_work_j = 0.0;
        expect_work_mutation_rejected(
            std::move(input),
            AdjacentCycleBlockConvergenceInputIssue::nonpositive_aggregate_loss_work,
            "nonpositive aggregate loss work was admitted");
    }
    {
        auto input = valid;
        input.starter_work_j = -0.0;
        expect_work_mutation_rejected(
            std::move(input),
            AdjacentCycleBlockConvergenceInputIssue::noncanonical_starter_work,
            "negative-zero starter work was admitted");
    }
    {
        auto input = valid;
        input.brake_work_j =
            std::nextafter(input.brake_work_j, std::numeric_limits<double>::infinity());
        expect_work_mutation_rejected(
            std::move(input),
            AdjacentCycleBlockConvergenceInputIssue::incoherent_brake_work,
            "incoherent brake-work identity was admitted");
    }
}

void run_tests() {
    test_method_identity_and_plan_admission();
    test_exact_window_math_and_inclusive_boundaries();
    test_latest_window_no_early_success_and_nonconvergence();
    test_first_ascending_pressure_tie_and_inclusive_comparison();
    test_insufficient_eligible_cycles();
    test_stable_chronological_pressure_reduction();
    test_malformed_cycle_inputs_are_terminal();
    test_work_lane_mutations_fail_closed();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "adjacent cycle-block convergence test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
