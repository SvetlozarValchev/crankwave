#include "simulation/adjacent_cycle_block_convergence.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <new>
#include <numbers>
#include <span>
#include <stdexcept>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

constexpr double kFourStrokeCycleRadians = 4.0 * std::numbers::pi_v<double>;

[[nodiscard]] consteval bool
canonical_lf_descriptor(std::string_view descriptor) noexcept {
    if (descriptor.empty() || descriptor.back() != '\n') {
        return false;
    }
    for (const char character : descriptor) {
        if (character == '\r' || character == '\0') {
            return false;
        }
    }
    return true;
}

constexpr std::string_view kAdjacentCycleBlockMeanConvergenceDescriptor =
    R"method(engine-sim-offline.simulation-method-configuration.v1
method=adjacent-nonoverlapping-cycle-block-mean-v1
version=1
operation=fixed-cutoff-adjacent-nonoverlapping-complete-cycle-block-mean-convergence
plan_method_identity=must-exactly-equal-this-descriptor-content-identity
plan_cycles_per_block=positive-u32-N
plan_retained_cycle_capacity=checked-u32-two-times-N
plan_time_domain=finite-binary64-eligibility-threshold-time-s-greater-than-or-equal-to-positive-zero-and-finite-fixed-cutoff-time-s-greater-than-or-equal-to-threshold
plan_tolerance_domain=finite-positive-binary64-cycle-mean-torque-tolerance-nm-and-pressure-tolerance-pa
plan_pressure_inventory=nonempty-strictly-increasing-nonzero-stable-gas-volume-id-array
cycle_input=owned-complete-cycle-ordinal,start-and-end-boundary-evidence-theta-rad-and-time-s,four-coherent-work-lanes,and-owned-end-boundary-pressure-array
cycle_work_domain=finite-binary64-indicated-gas-work-j,finite-positive-binary64-aggregate-loss-work-j,canonical-positive-zero-starter-work-j,and-finite-binary64-brake-work-j
cycle_brake_work_identity=brake-work-j-must-bit-equal-(indicated-gas-work-j-minus-positive-aggregate-loss-work-j)-plus-starter-work-j-in-written-order
boundary_evidence=exact-sample-has-equal-left-right-sample-index-and-positive-zero-fraction;bracketed-sample-has-left-index-less-than-right-index-and-finite-fraction-strictly-between-zero-and-one
cycle_extent=finite-start-and-end-theta-rad-and-time-s;start-time-nonnegative;end-time-and-theta-strictly-increase;end-boundary-right-sample-index-strictly-increases
cycle_chronology=after-first-input-ordinal-increments-exactly-one-and-start-boundary-evidence-theta-and-time-exactly-equal-previous-end-boundary
cycle_pressure_input=exact-plan-shape-and-identities-in-ascending-order;every-absolute-pressure-finite-and-positive
eligibility=cycle-start-time-s-greater-than-or-equal-to-threshold-and-cycle-end-time-s-less-than-or-equal-to-fixed-cutoff
window=retain-only-latest-two-times-N-eligible-complete-cycles-in-chronological-order
finalization=observe-never-reports-convergence;evaluate-exactly-on-explicit-fixed-cutoff-finalization
block-a=older-N-cycles-of-retained-window
block-b=newer-N-cycles-of-retained-window
work-reduction=separate-left-to-right-binary64-sums-in-cycle-chronology-for-indicated-gas,positive-aggregate-loss,starter,and-brake-work-in-each-block
block-work-evidence=retain-all-four-work-lane-sums
block-mean-brake-torque-nm=sum-brake-work-j-divided-by-(binary64(N)-times-binary64-four-times-std-numbers-pi-v-binary64)-in-written-order
pressure-reduction=for-each-ascending-gas-volume-id-separate-left-to-right-binary64-sum-in-cycle-chronology-for-each-block-then-divide-by-binary64(N)
torque-residual-nm=abs(block-b-mean-brake-torque-nm-minus-block-a-mean-brake-torque-nm)
pressure-residual-pa=maximum-in-ascending-gas-volume-id-order-of-abs(block-b-mean-pressure-minus-block-a-mean-pressure)
pressure-residual-tie=retain-first-ascending-stable-gas-volume-id
comparison=settled-only-if-torque-residual-less-than-or-equal-to-torque-tolerance-and-pressure-residual-less-than-or-equal-to-pressure-tolerance
insufficient-window=typed-terminal-insufficient-cycles-error
failed-comparison=typed-terminal-nonconverged-error-retaining-complete-evaluated-evidence
malformed-input=first-typed-terminal-error;subsequent-observe-and-finalize-return-stored-error
successful-finalization=terminal-result;subsequent-finalize-returns-stored-result-and-observe-reports-closed
parallel-reduction=none
binary64_execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-abs-and-std-numbers-pi-v-binary64-under-render-determinism-envelope
external_numeric_authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(canonical_lf_descriptor(kAdjacentCycleBlockMeanConvergenceDescriptor));

[[nodiscard]] contract::Sha256Digest
descriptor_digest(std::string_view descriptor) noexcept {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

[[nodiscard]] AdjacentCycleBlockConvergenceError
plan_error(AdjacentCycleBlockConvergencePlanIssue issue) {
    AdjacentCycleBlockConvergenceError error;
    error.code = AdjacentCycleBlockConvergenceErrorCode::invalid_plan;
    error.plan_issue = issue;
    return error;
}

[[nodiscard]] AdjacentCycleBlockConvergenceError
capacity_error(std::size_t retained_cycle_count = 0,
               std::size_t required_cycle_count = 0) {
    AdjacentCycleBlockConvergenceError error;
    error.code = AdjacentCycleBlockConvergenceErrorCode::capacity_overflow;
    error.retained_cycle_count = retained_cycle_count;
    error.required_cycle_count = required_cycle_count;
    return error;
}

[[nodiscard]] bool
valid_boundary_evidence(const CycleBoundaryEvidence &evidence) noexcept {
    if (evidence.left_bracket_sample_index > evidence.right_bracket_sample_index) {
        return false;
    }
    if (evidence.left_bracket_sample_index == evidence.right_bracket_sample_index) {
        return evidence.fraction_from_left_01 == 0.0 &&
               !std::signbit(evidence.fraction_from_left_01);
    }
    return std::isfinite(evidence.fraction_from_left_01) &&
           evidence.fraction_from_left_01 > 0.0 && evidence.fraction_from_left_01 < 1.0;
}

} // namespace

std::string_view adjacent_cycle_block_mean_convergence_method_descriptor() noexcept {
    return kAdjacentCycleBlockMeanConvergenceDescriptor;
}

const contract::MethodIdentity &
adjacent_cycle_block_mean_convergence_method_identity() {
    static const contract::MethodIdentity identity{
        std::string{kAdjacentCycleBlockMeanConvergenceMethodId},
        kAdjacentCycleBlockMeanConvergenceMethodVersion,
        descriptor_digest(kAdjacentCycleBlockMeanConvergenceDescriptor),
    };
    return identity;
}

AdjacentCycleBlockConvergenceObserver::AdjacentCycleBlockConvergenceObserver(
    AdjacentCycleBlockConvergencePlan plan, std::size_t retained_cycle_capacity)
    : plan_(std::move(plan)), retained_cycle_capacity_(retained_cycle_capacity) {}

AdjacentCycleBlockConvergenceError AdjacentCycleBlockConvergenceObserver::malformed(
    AdjacentCycleBlockConvergenceInputIssue issue, const AdjacentCycleBlockCycle &cycle,
    std::size_t element_index) {
    AdjacentCycleBlockConvergenceError error;
    error.code = AdjacentCycleBlockConvergenceErrorCode::malformed_cycle_input;
    error.input_issue = issue;
    error.cycle_ordinal = cycle.completed_cycle_ordinal;
    error.element_index = element_index;
    error.retained_cycle_count = retained_cycles_.size();
    error.required_cycle_count = retained_cycle_capacity_;
    return error;
}

AdjacentCycleBlockObservationResult
AdjacentCycleBlockConvergenceObserver::fail_observation(
    AdjacentCycleBlockConvergenceError error) {
    if (!terminal_error_.has_value()) {
        terminal_error_ = std::move(error);
    }
    return *terminal_error_;
}

AdjacentCycleBlockFinalizationResult
AdjacentCycleBlockConvergenceObserver::fail_finalization(
    AdjacentCycleBlockConvergenceError error) {
    if (!terminal_error_.has_value()) {
        terminal_error_ = std::move(error);
    }
    return *terminal_error_;
}

AdjacentCycleBlockObservationResult
AdjacentCycleBlockConvergenceObserver::observe(const AdjacentCycleBlockCycle &cycle) {
    if (terminal_error_.has_value()) {
        return *terminal_error_;
    }
    if (terminal_result_.has_value()) {
        return AdjacentCycleBlockObservationClosed{};
    }

    if (!valid_boundary_evidence(cycle.start_boundary.interpolation) ||
        !valid_boundary_evidence(cycle.end_boundary.interpolation) ||
        cycle.start_boundary.interpolation.right_bracket_sample_index >=
            cycle.end_boundary.interpolation.right_bracket_sample_index) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::invalid_boundary_evidence, cycle));
    }

    const bool finite_cycle_scalars = std::isfinite(cycle.start_boundary.theta_rad) &&
                                      std::isfinite(cycle.start_boundary.time_s) &&
                                      std::isfinite(cycle.end_boundary.theta_rad) &&
                                      std::isfinite(cycle.end_boundary.time_s);
    if (!finite_cycle_scalars) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::nonfinite_cycle_scalar, cycle));
    }
    if (cycle.start_boundary.time_s < 0.0 ||
        !(cycle.end_boundary.time_s > cycle.start_boundary.time_s) ||
        !(cycle.end_boundary.theta_rad > cycle.start_boundary.theta_rad)) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::nonpositive_cycle_extent, cycle));
    }

    const std::array cycle_works{
        cycle.indicated_gas_work_j,
        cycle.positive_aggregate_loss_work_j,
        cycle.starter_work_j,
        cycle.brake_work_j,
    };
    for (const double work_j : cycle_works) {
        if (!std::isfinite(work_j)) {
            return fail_observation(malformed(
                AdjacentCycleBlockConvergenceInputIssue::nonfinite_cycle_work, cycle));
        }
    }
    if (!(cycle.positive_aggregate_loss_work_j > 0.0)) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::nonpositive_aggregate_loss_work,
            cycle));
    }
    if (cycle.starter_work_j != 0.0 || std::signbit(cycle.starter_work_j)) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::noncanonical_starter_work, cycle));
    }
    const double coherent_brake_work_j =
        (cycle.indicated_gas_work_j - cycle.positive_aggregate_loss_work_j) +
        cycle.starter_work_j;
    if (std::bit_cast<std::uint64_t>(cycle.brake_work_j) !=
        std::bit_cast<std::uint64_t>(coherent_brake_work_j)) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::incoherent_brake_work, cycle));
    }

    if (previous_cycle_.has_value()) {
        if (previous_cycle_->completed_cycle_ordinal ==
                std::numeric_limits<std::uint64_t>::max() ||
            cycle.completed_cycle_ordinal !=
                previous_cycle_->completed_cycle_ordinal + 1U) {
            return fail_observation(malformed(
                AdjacentCycleBlockConvergenceInputIssue::noncontiguous_cycle_ordinal,
                cycle));
        }
        if (cycle.start_boundary != previous_cycle_->end_boundary) {
            return fail_observation(malformed(
                AdjacentCycleBlockConvergenceInputIssue::noncontiguous_cycle_boundary,
                cycle));
        }
    }

    if (cycle.end_boundary_pressures.size() != plan_.physical_gas_volume_ids.size()) {
        return fail_observation(malformed(
            AdjacentCycleBlockConvergenceInputIssue::pressure_shape_mismatch, cycle));
    }

    contract::GasVolumeId previous_id;
    for (std::size_t index = 0; index < cycle.end_boundary_pressures.size(); ++index) {
        const auto &pressure = cycle.end_boundary_pressures[index];
        if (!pressure.gas_volume_id.valid()) {
            return fail_observation(malformed(
                AdjacentCycleBlockConvergenceInputIssue::invalid_pressure_identity,
                cycle, index));
        }
        if (index != 0 && pressure.gas_volume_id <= previous_id) {
            return fail_observation(malformed(AdjacentCycleBlockConvergenceInputIssue::
                                                  unstable_pressure_identity_order,
                                              cycle, index));
        }
        if (pressure.gas_volume_id != plan_.physical_gas_volume_ids[index]) {
            return fail_observation(malformed(
                AdjacentCycleBlockConvergenceInputIssue::unexpected_pressure_identity,
                cycle, index));
        }
        if (!std::isfinite(pressure.pressure_pa_abs)) {
            return fail_observation(
                malformed(AdjacentCycleBlockConvergenceInputIssue::nonfinite_pressure,
                          cycle, index));
        }
        if (!(pressure.pressure_pa_abs > 0.0)) {
            return fail_observation(
                malformed(AdjacentCycleBlockConvergenceInputIssue::nonpositive_pressure,
                          cycle, index));
        }
        previous_id = pressure.gas_volume_id;
    }

    const bool eligible =
        cycle.start_boundary.time_s >= plan_.eligibility_threshold_time_s &&
        cycle.end_boundary.time_s <= plan_.fixed_cutoff_time_s;

    if (eligible) {
        try {
            StoredCycle retained{
                cycle.completed_cycle_ordinal,
                cycle.start_boundary,
                cycle.end_boundary,
                cycle.indicated_gas_work_j,
                cycle.positive_aggregate_loss_work_j,
                cycle.starter_work_j,
                cycle.brake_work_j,
                {},
            };
            retained.end_boundary_pressures_pa_abs.reserve(
                cycle.end_boundary_pressures.size());
            for (const auto &pressure : cycle.end_boundary_pressures) {
                retained.end_boundary_pressures_pa_abs.push_back(
                    pressure.pressure_pa_abs);
            }
            if (retained_cycles_.size() == retained_cycle_capacity_) {
                retained_cycles_.pop_front();
            }
            retained_cycles_.push_back(std::move(retained));
        } catch (const std::bad_alloc &) {
            return fail_observation(
                capacity_error(retained_cycles_.size(), retained_cycle_capacity_));
        } catch (const std::length_error &) {
            return fail_observation(
                capacity_error(retained_cycles_.size(), retained_cycle_capacity_));
        }
    }

    previous_cycle_ = StoredCycle{
        cycle.completed_cycle_ordinal,
        cycle.start_boundary,
        cycle.end_boundary,
        cycle.indicated_gas_work_j,
        cycle.positive_aggregate_loss_work_j,
        cycle.starter_work_j,
        cycle.brake_work_j,
        {},
    };
    return AdjacentCycleBlockObservationAccepted{
        eligible,
        retained_cycles_.size(),
    };
}

AdjacentCycleBlockFinalizationResult
AdjacentCycleBlockConvergenceObserver::finalize_at_fixed_cutoff() {
    if (terminal_error_.has_value()) {
        return *terminal_error_;
    }
    if (terminal_result_.has_value()) {
        return *terminal_result_;
    }
    if (retained_cycles_.size() != retained_cycle_capacity_) {
        AdjacentCycleBlockConvergenceError error;
        error.code = AdjacentCycleBlockConvergenceErrorCode::insufficient_cycles;
        error.retained_cycle_count = retained_cycles_.size();
        error.required_cycle_count = retained_cycle_capacity_;
        return fail_finalization(std::move(error));
    }

    try {
        const std::size_t cycles_per_block = plan_.cycles_per_block;
        const std::size_t volume_count = plan_.physical_gas_volume_ids.size();
        std::vector<double> block_a_pressure_sums(volume_count, 0.0);
        std::vector<double> block_b_pressure_sums(volume_count, 0.0);
        struct WorkSums {
            double indicated_gas_work_j = 0.0;
            double positive_aggregate_loss_work_j = 0.0;
            double starter_work_j = 0.0;
            double brake_work_j = 0.0;
        };
        WorkSums block_a_work;
        WorkSums block_b_work;
        const auto accumulate_work = [](WorkSums &sum, const StoredCycle &cycle) {
            sum.indicated_gas_work_j += cycle.indicated_gas_work_j;
            sum.positive_aggregate_loss_work_j += cycle.positive_aggregate_loss_work_j;
            sum.starter_work_j += cycle.starter_work_j;
            sum.brake_work_j += cycle.brake_work_j;
            return std::isfinite(sum.indicated_gas_work_j) &&
                   std::isfinite(sum.positive_aggregate_loss_work_j) &&
                   std::isfinite(sum.starter_work_j) && std::isfinite(sum.brake_work_j);
        };

        for (std::size_t cycle_index = 0; cycle_index < cycles_per_block;
             ++cycle_index) {
            const auto &cycle = retained_cycles_[cycle_index];
            if (!accumulate_work(block_a_work, cycle)) {
                AdjacentCycleBlockConvergenceError error;
                error.code = AdjacentCycleBlockConvergenceErrorCode::nonfinite_result;
                error.cycle_ordinal = cycle.completed_cycle_ordinal;
                return fail_finalization(std::move(error));
            }
            for (std::size_t volume_index = 0; volume_index < volume_count;
                 ++volume_index) {
                block_a_pressure_sums[volume_index] +=
                    cycle.end_boundary_pressures_pa_abs[volume_index];
                if (!std::isfinite(block_a_pressure_sums[volume_index])) {
                    AdjacentCycleBlockConvergenceError error;
                    error.code =
                        AdjacentCycleBlockConvergenceErrorCode::nonfinite_result;
                    error.cycle_ordinal = cycle.completed_cycle_ordinal;
                    error.element_index = volume_index;
                    return fail_finalization(std::move(error));
                }
            }
        }
        for (std::size_t cycle_index = cycles_per_block;
             cycle_index < retained_cycle_capacity_; ++cycle_index) {
            const auto &cycle = retained_cycles_[cycle_index];
            if (!accumulate_work(block_b_work, cycle)) {
                AdjacentCycleBlockConvergenceError error;
                error.code = AdjacentCycleBlockConvergenceErrorCode::nonfinite_result;
                error.cycle_ordinal = cycle.completed_cycle_ordinal;
                return fail_finalization(std::move(error));
            }
            for (std::size_t volume_index = 0; volume_index < volume_count;
                 ++volume_index) {
                block_b_pressure_sums[volume_index] +=
                    cycle.end_boundary_pressures_pa_abs[volume_index];
                if (!std::isfinite(block_b_pressure_sums[volume_index])) {
                    AdjacentCycleBlockConvergenceError error;
                    error.code =
                        AdjacentCycleBlockConvergenceErrorCode::nonfinite_result;
                    error.cycle_ordinal = cycle.completed_cycle_ordinal;
                    error.element_index = volume_index;
                    return fail_finalization(std::move(error));
                }
            }
        }

        const double binary64_cycles_per_block =
            static_cast<double>(plan_.cycles_per_block);
        const double torque_denominator =
            binary64_cycles_per_block * kFourStrokeCycleRadians;
        const double block_a_mean_torque_nm =
            block_a_work.brake_work_j / torque_denominator;
        const double block_b_mean_torque_nm =
            block_b_work.brake_work_j / torque_denominator;
        const double torque_residual_nm =
            std::abs(block_b_mean_torque_nm - block_a_mean_torque_nm);
        if (!std::isfinite(torque_denominator) ||
            !std::isfinite(block_a_mean_torque_nm) ||
            !std::isfinite(block_b_mean_torque_nm) ||
            !std::isfinite(torque_residual_nm)) {
            AdjacentCycleBlockConvergenceError error;
            error.code = AdjacentCycleBlockConvergenceErrorCode::nonfinite_result;
            return fail_finalization(std::move(error));
        }

        std::vector<AdjacentCycleBlockPressureMeans> pressure_means;
        pressure_means.reserve(volume_count);
        double pressure_residual_pa = 0.0;
        contract::GasVolumeId limiting_gas_volume_id =
            plan_.physical_gas_volume_ids.front();
        for (std::size_t volume_index = 0; volume_index < volume_count;
             ++volume_index) {
            const double block_a_mean_pressure_pa_abs =
                block_a_pressure_sums[volume_index] / binary64_cycles_per_block;
            const double block_b_mean_pressure_pa_abs =
                block_b_pressure_sums[volume_index] / binary64_cycles_per_block;
            const double residual_pa =
                std::abs(block_b_mean_pressure_pa_abs - block_a_mean_pressure_pa_abs);
            if (!std::isfinite(block_a_mean_pressure_pa_abs) ||
                !std::isfinite(block_b_mean_pressure_pa_abs) ||
                !std::isfinite(residual_pa)) {
                AdjacentCycleBlockConvergenceError error;
                error.code = AdjacentCycleBlockConvergenceErrorCode::nonfinite_result;
                error.element_index = volume_index;
                return fail_finalization(std::move(error));
            }
            pressure_means.push_back({
                plan_.physical_gas_volume_ids[volume_index],
                block_a_mean_pressure_pa_abs,
                block_b_mean_pressure_pa_abs,
            });
            if (volume_index == 0 || residual_pa > pressure_residual_pa) {
                pressure_residual_pa = residual_pa;
                limiting_gas_volume_id = plan_.physical_gas_volume_ids[volume_index];
            }
        }

        const bool settled =
            torque_residual_nm <= plan_.cycle_mean_torque_tolerance_nm &&
            pressure_residual_pa <= plan_.pressure_tolerance_pa;
        const auto cycle_range = [](const StoredCycle &first, const StoredCycle &last) {
            return AdjacentCycleBlockRange{
                first.completed_cycle_ordinal,
                last.completed_cycle_ordinal,
                first.start_boundary,
                last.end_boundary,
            };
        };
        AdjacentCycleBlockConvergenceEvidence evidence{
            plan_.method,
            plan_.cycles_per_block,
            plan_.eligibility_threshold_time_s,
            plan_.fixed_cutoff_time_s,
            {
                cycle_range(retained_cycles_.front(),
                            retained_cycles_[cycles_per_block - 1U]),
                block_a_work.indicated_gas_work_j,
                block_a_work.positive_aggregate_loss_work_j,
                block_a_work.starter_work_j,
                block_a_work.brake_work_j,
                block_a_mean_torque_nm,
            },
            {
                cycle_range(retained_cycles_[cycles_per_block],
                            retained_cycles_.back()),
                block_b_work.indicated_gas_work_j,
                block_b_work.positive_aggregate_loss_work_j,
                block_b_work.starter_work_j,
                block_b_work.brake_work_j,
                block_b_mean_torque_nm,
            },
            torque_residual_nm,
            plan_.cycle_mean_torque_tolerance_nm,
            std::move(pressure_means),
            pressure_residual_pa,
            limiting_gas_volume_id,
            plan_.pressure_tolerance_pa,
            settled,
        };

        if (!settled) {
            AdjacentCycleBlockConvergenceError error;
            error.code = AdjacentCycleBlockConvergenceErrorCode::nonconverged;
            error.retained_cycle_count = retained_cycles_.size();
            error.required_cycle_count = retained_cycle_capacity_;
            error.evidence = std::move(evidence);
            return fail_finalization(std::move(error));
        }
        terminal_result_ = AdjacentCycleBlockConverged{std::move(evidence)};
        return *terminal_result_;
    } catch (const std::bad_alloc &) {
        return fail_finalization(
            capacity_error(retained_cycles_.size(), retained_cycle_capacity_));
    } catch (const std::length_error &) {
        return fail_finalization(
            capacity_error(retained_cycles_.size(), retained_cycle_capacity_));
    }
}

std::size_t
AdjacentCycleBlockConvergenceObserver::retained_eligible_cycle_count() const noexcept {
    return retained_cycles_.size();
}

std::size_t
AdjacentCycleBlockConvergenceObserver::retained_cycle_capacity() const noexcept {
    return retained_cycle_capacity_;
}

bool AdjacentCycleBlockConvergenceObserver::finalized() const noexcept {
    return terminal_result_.has_value() || terminal_error_.has_value();
}

bool AdjacentCycleBlockConvergenceObserver::faulted() const noexcept {
    return terminal_error_.has_value();
}

AdjacentCycleBlockConvergenceCompileResult
compile_adjacent_cycle_block_convergence_observer(
    AdjacentCycleBlockConvergencePlan plan) {
    if (plan.method != adjacent_cycle_block_mean_convergence_method_identity()) {
        return plan_error(
            AdjacentCycleBlockConvergencePlanIssue::unsupported_method_identity);
    }
    if (plan.cycles_per_block == 0U) {
        return plan_error(AdjacentCycleBlockConvergencePlanIssue::invalid_cycle_count);
    }
    if (!std::isfinite(plan.eligibility_threshold_time_s) ||
        !std::isfinite(plan.fixed_cutoff_time_s) ||
        plan.eligibility_threshold_time_s < 0.0 ||
        plan.fixed_cutoff_time_s < plan.eligibility_threshold_time_s) {
        return plan_error(AdjacentCycleBlockConvergencePlanIssue::invalid_time_bounds);
    }
    if (!std::isfinite(plan.cycle_mean_torque_tolerance_nm) ||
        !(plan.cycle_mean_torque_tolerance_nm > 0.0)) {
        return plan_error(
            AdjacentCycleBlockConvergencePlanIssue::invalid_torque_tolerance);
    }
    if (!std::isfinite(plan.pressure_tolerance_pa) ||
        !(plan.pressure_tolerance_pa > 0.0)) {
        return plan_error(
            AdjacentCycleBlockConvergencePlanIssue::invalid_pressure_tolerance);
    }
    if (plan.physical_gas_volume_ids.empty()) {
        return plan_error(
            AdjacentCycleBlockConvergencePlanIssue::empty_pressure_inventory);
    }
    contract::GasVolumeId previous_id;
    for (std::size_t index = 0; index < plan.physical_gas_volume_ids.size(); ++index) {
        const auto id = plan.physical_gas_volume_ids[index];
        if (!id.valid()) {
            auto error = plan_error(
                AdjacentCycleBlockConvergencePlanIssue::invalid_pressure_identity);
            error.element_index = index;
            return error;
        }
        if (index != 0 && id <= previous_id) {
            auto error = plan_error(AdjacentCycleBlockConvergencePlanIssue::
                                        unstable_pressure_identity_order);
            error.element_index = index;
            return error;
        }
        previous_id = id;
    }

    if (plan.cycles_per_block > std::numeric_limits<std::uint32_t>::max() / 2U) {
        return capacity_error();
    }
    const std::size_t retained_cycle_capacity =
        static_cast<std::size_t>(plan.cycles_per_block * 2U);
    if (plan.physical_gas_volume_ids.size() >
        std::numeric_limits<std::size_t>::max() / retained_cycle_capacity) {
        return capacity_error(0, retained_cycle_capacity);
    }

    try {
        return AdjacentCycleBlockConvergenceObserver{
            std::move(plan),
            retained_cycle_capacity,
        };
    } catch (const std::bad_alloc &) {
        return capacity_error(0, retained_cycle_capacity);
    } catch (const std::length_error &) {
        return capacity_error(0, retained_cycle_capacity);
    }
}

} // namespace engine_sim_offline::simulation
