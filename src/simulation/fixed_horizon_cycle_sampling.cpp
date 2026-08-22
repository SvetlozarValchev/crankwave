#include "simulation/fixed_horizon_cycle_sampling.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <new>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace crankwave::simulation {
namespace {

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

constexpr std::string_view kFixedHorizonCycleSamplingDescriptor =
    R"method(crankwave.simulation-method-configuration.v1
method=fixed-horizon-trailing-complete-cycle-sample-v1
version=1
operation=fixed-preparation-horizon-trailing-complete-cycle-sampling
plan-method-identity=must-exactly-equal-this-descriptor-content-identity
plan-fixed-preparation-horizon-s=finite-positive-binary64-on-the-physics-frame-grid
plan-trailing-complete-cycle-count=positive-u32-M
cycle-input=owned-complete-cycle-ordinal,start-and-end-boundary-evidence-theta-rad-and-time-s,four-coherent-work-lanes,and-owned-end-boundary-pressure-array
cycle-work-domain=finite-binary64-indicated-gas-work-j,finite-positive-binary64-aggregate-loss-work-j,canonical-positive-zero-starter-work-j,and-finite-binary64-brake-work-j
cycle-brake-work-identity=brake-work-j-must-bit-equal-(indicated-gas-work-j-minus-positive-aggregate-loss-work-j)-plus-starter-work-j-in-written-order
boundary-evidence=exact-sample-has-equal-left-right-sample-index-and-positive-zero-fraction;bracketed-sample-has-left-index-less-than-right-index-and-finite-fraction-strictly-between-zero-and-one
cycle-extent=finite-start-and-end-theta-rad-and-time-s;start-time-nonnegative;end-time-and-theta-strictly-increase;end-boundary-right-sample-index-strictly-increases
cycle-chronology=after-first-input-ordinal-increments-exactly-one-and-start-boundary-evidence-theta-and-time-exactly-equal-previous-end-boundary
cycle-pressure-input=exact-plan-shape-and-identities-in-ascending-order;every-absolute-pressure-finite-and-positive
eligibility=complete-cycle-end-time-s-less-than-or-equal-to-fixed-preparation-horizon-s
window=retain-only-latest-M-eligible-complete-cycles-in-chronological-order
finalization=observe-never-publishes-a-result;evaluate-only-on-explicit-fixed-horizon-finalization
work-reduction=separate-left-to-right-binary64-sums-in-cycle-chronology-for-indicated-gas,positive-aggregate-loss,starter,and-brake-work
mean-brake-torque-nm=sum-brake-work-j-divided-by-(binary64(M)-times-binary64-four-times-std-numbers-pi-v-binary64)-in-written-order
pressure-reduction=for-each-ascending-gas-volume-id-left-to-right-binary64-sum-in-cycle-chronology-then-divide-by-binary64(M)
insufficient-window=typed-terminal-insufficient-cycles-error
malformed-input=first-typed-terminal-error;subsequent-observe-and-finalize-return-stored-error
successful-finalization=one-terminal-fixed-sample-result;subsequent-finalize-returns-stored-result-and-observe-reports-closed
stationarity-claim=none
parallel-reduction=none
binary64-execution=ieee754-binary64-nearest-ties-to-even-no-fma-no-ftz-no-daz
transcendentals=std-numbers-pi-v-binary64-under-render-determinism-envelope
external-numeric-authority=renderer-build-source-standard-library-math-runtime-and-thread-numeric-environment-identities
)method";

static_assert(canonical_lf_descriptor(kFixedHorizonCycleSamplingDescriptor));

[[nodiscard]] FixedHorizonCycleSamplingError
plan_error(FixedHorizonCycleSamplingPlanIssue issue) {
    FixedHorizonCycleSamplingError error;
    error.code = FixedHorizonCycleSamplingErrorCode::invalid_plan;
    error.plan_issue = issue;
    return error;
}

[[nodiscard]] FixedHorizonCycleSamplingError
capacity_error(std::size_t retained_cycle_count = 0,
               std::size_t required_cycle_count = 0) {
    FixedHorizonCycleSamplingError error;
    error.code = FixedHorizonCycleSamplingErrorCode::capacity_overflow;
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

std::string_view fixed_horizon_cycle_sampling_method_descriptor() noexcept {
    return kFixedHorizonCycleSamplingDescriptor;
}

const contract::MethodIdentity &fixed_horizon_cycle_sampling_method_identity() {
    return contract::fixed_horizon_cycle_sampling_method_identity();
}

FixedHorizonCycleSampler::FixedHorizonCycleSampler(FixedHorizonCycleSamplerPlan plan,
                                                   std::size_t retained_cycle_capacity)
    : plan_(std::move(plan)), retained_cycle_capacity_(retained_cycle_capacity) {}

FixedHorizonCycleSamplingError
FixedHorizonCycleSampler::malformed(FixedHorizonCycleSamplingInputIssue issue,
                                    const FixedHorizonCycle &cycle,
                                    std::size_t element_index) {
    FixedHorizonCycleSamplingError error;
    error.code = FixedHorizonCycleSamplingErrorCode::malformed_cycle_input;
    error.input_issue = issue;
    error.cycle_ordinal = cycle.completed_cycle_ordinal;
    error.element_index = element_index;
    error.retained_cycle_count = retained_cycles_.size();
    error.required_cycle_count = retained_cycle_capacity_;
    return error;
}

FixedHorizonCycleObservationResult
FixedHorizonCycleSampler::fail_observation(FixedHorizonCycleSamplingError error) {
    if (!terminal_error_.has_value()) {
        terminal_error_ = std::move(error);
    }
    return *terminal_error_;
}

FixedHorizonCycleFinalizationResult
FixedHorizonCycleSampler::fail_finalization(FixedHorizonCycleSamplingError error) {
    if (!terminal_error_.has_value()) {
        terminal_error_ = std::move(error);
    }
    return *terminal_error_;
}

FixedHorizonCycleObservationResult
FixedHorizonCycleSampler::observe(const FixedHorizonCycle &cycle) {
    if (terminal_error_.has_value()) {
        return *terminal_error_;
    }
    if (terminal_result_.has_value()) {
        return FixedHorizonCycleObservationClosed{};
    }

    if (!valid_boundary_evidence(cycle.start_boundary.interpolation) ||
        !valid_boundary_evidence(cycle.end_boundary.interpolation) ||
        cycle.start_boundary.interpolation.right_bracket_sample_index >=
            cycle.end_boundary.interpolation.right_bracket_sample_index) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::invalid_boundary_evidence, cycle));
    }

    if (!std::isfinite(cycle.start_boundary.theta_rad) ||
        !std::isfinite(cycle.start_boundary.time_s) ||
        !std::isfinite(cycle.end_boundary.theta_rad) ||
        !std::isfinite(cycle.end_boundary.time_s)) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::nonfinite_cycle_scalar, cycle));
    }
    if (cycle.start_boundary.time_s < 0.0 ||
        !(cycle.end_boundary.time_s > cycle.start_boundary.time_s) ||
        !(cycle.end_boundary.theta_rad > cycle.start_boundary.theta_rad)) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::nonpositive_cycle_extent, cycle));
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
                FixedHorizonCycleSamplingInputIssue::nonfinite_cycle_work, cycle));
        }
    }
    if (!(cycle.positive_aggregate_loss_work_j > 0.0)) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::nonpositive_aggregate_loss_work,
            cycle));
    }
    if (cycle.starter_work_j != 0.0 || std::signbit(cycle.starter_work_j)) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::noncanonical_starter_work, cycle));
    }
    const double coherent_brake_work_j =
        (cycle.indicated_gas_work_j - cycle.positive_aggregate_loss_work_j) +
        cycle.starter_work_j;
    if (std::bit_cast<std::uint64_t>(cycle.brake_work_j) !=
        std::bit_cast<std::uint64_t>(coherent_brake_work_j)) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::incoherent_brake_work, cycle));
    }

    if (previous_cycle_.has_value()) {
        if (previous_cycle_->completed_cycle_ordinal ==
                std::numeric_limits<std::uint64_t>::max() ||
            cycle.completed_cycle_ordinal !=
                previous_cycle_->completed_cycle_ordinal + 1U) {
            return fail_observation(malformed(
                FixedHorizonCycleSamplingInputIssue::noncontiguous_cycle_ordinal,
                cycle));
        }
        if (cycle.start_boundary != previous_cycle_->end_boundary) {
            return fail_observation(malformed(
                FixedHorizonCycleSamplingInputIssue::noncontiguous_cycle_boundary,
                cycle));
        }
    }

    if (cycle.end_boundary_pressures.size() != plan_.physical_gas_volume_ids.size()) {
        return fail_observation(malformed(
            FixedHorizonCycleSamplingInputIssue::pressure_shape_mismatch, cycle));
    }

    contract::GasVolumeId previous_id;
    for (std::size_t index = 0; index < cycle.end_boundary_pressures.size(); ++index) {
        const auto &pressure = cycle.end_boundary_pressures[index];
        if (!pressure.gas_volume_id.valid()) {
            return fail_observation(malformed(
                FixedHorizonCycleSamplingInputIssue::invalid_pressure_identity, cycle,
                index));
        }
        if (index != 0 && pressure.gas_volume_id <= previous_id) {
            return fail_observation(malformed(
                FixedHorizonCycleSamplingInputIssue::unstable_pressure_identity_order,
                cycle, index));
        }
        if (pressure.gas_volume_id != plan_.physical_gas_volume_ids[index]) {
            return fail_observation(malformed(
                FixedHorizonCycleSamplingInputIssue::unexpected_pressure_identity,
                cycle, index));
        }
        if (!std::isfinite(pressure.pressure_pa_abs)) {
            return fail_observation(malformed(
                FixedHorizonCycleSamplingInputIssue::nonfinite_pressure, cycle, index));
        }
        if (!(pressure.pressure_pa_abs > 0.0)) {
            return fail_observation(
                malformed(FixedHorizonCycleSamplingInputIssue::nonpositive_pressure,
                          cycle, index));
        }
        previous_id = pressure.gas_volume_id;
    }

    const bool eligible =
        cycle.end_boundary.time_s <= plan_.fixed_preparation_horizon_s;
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
    return FixedHorizonCycleObservationAccepted{
        eligible,
        retained_cycles_.size(),
    };
}

FixedHorizonCycleFinalizationResult
FixedHorizonCycleSampler::finalize_at_fixed_horizon() {
    if (terminal_error_.has_value()) {
        return *terminal_error_;
    }
    if (terminal_result_.has_value()) {
        return *terminal_result_;
    }
    if (retained_cycles_.size() != retained_cycle_capacity_) {
        FixedHorizonCycleSamplingError error;
        error.code = FixedHorizonCycleSamplingErrorCode::insufficient_cycles;
        error.retained_cycle_count = retained_cycles_.size();
        error.required_cycle_count = retained_cycle_capacity_;
        return fail_finalization(std::move(error));
    }

    try {
        const std::size_t volume_count = plan_.physical_gas_volume_ids.size();
        std::vector<double> pressure_sums(volume_count, 0.0);
        std::vector<FixedHorizonCompletedCycle> completed_cycles;
        completed_cycles.reserve(retained_cycle_capacity_);
        double indicated_gas_work_j = 0.0;
        double positive_aggregate_loss_work_j = 0.0;
        double starter_work_j = 0.0;
        double brake_work_j = 0.0;

        for (const auto &cycle : retained_cycles_) {
            indicated_gas_work_j += cycle.indicated_gas_work_j;
            positive_aggregate_loss_work_j += cycle.positive_aggregate_loss_work_j;
            starter_work_j += cycle.starter_work_j;
            brake_work_j += cycle.brake_work_j;
            if (!std::isfinite(indicated_gas_work_j) ||
                !std::isfinite(positive_aggregate_loss_work_j) ||
                !std::isfinite(starter_work_j) || !std::isfinite(brake_work_j)) {
                FixedHorizonCycleSamplingError error;
                error.code = FixedHorizonCycleSamplingErrorCode::nonfinite_result;
                error.cycle_ordinal = cycle.completed_cycle_ordinal;
                return fail_finalization(std::move(error));
            }

            FixedHorizonCompletedCycle completed{
                cycle.completed_cycle_ordinal,
                cycle.indicated_gas_work_j,
                cycle.positive_aggregate_loss_work_j,
                cycle.starter_work_j,
                cycle.brake_work_j,
                {},
            };
            completed.end_boundary_pressures.reserve(volume_count);
            for (std::size_t volume_index = 0; volume_index < volume_count;
                 ++volume_index) {
                pressure_sums[volume_index] +=
                    cycle.end_boundary_pressures_pa_abs[volume_index];
                if (!std::isfinite(pressure_sums[volume_index])) {
                    FixedHorizonCycleSamplingError error;
                    error.code = FixedHorizonCycleSamplingErrorCode::nonfinite_result;
                    error.cycle_ordinal = cycle.completed_cycle_ordinal;
                    error.element_index = volume_index;
                    return fail_finalization(std::move(error));
                }
                completed.end_boundary_pressures.push_back({
                    plan_.physical_gas_volume_ids[volume_index],
                    cycle.end_boundary_pressures_pa_abs[volume_index],
                });
            }
            completed_cycles.push_back(std::move(completed));
        }

        const double binary64_cycle_count =
            static_cast<double>(plan_.trailing_complete_cycle_count);
        const double torque_denominator =
            binary64_cycle_count * 4.0 * std::numbers::pi_v<double>;
        const double mean_brake_torque_nm = brake_work_j / torque_denominator;
        if (!std::isfinite(torque_denominator) ||
            !std::isfinite(mean_brake_torque_nm)) {
            FixedHorizonCycleSamplingError error;
            error.code = FixedHorizonCycleSamplingErrorCode::nonfinite_result;
            return fail_finalization(std::move(error));
        }

        std::vector<FixedHorizonMeanBoundaryPressure> mean_pressures;
        mean_pressures.reserve(volume_count);
        for (std::size_t volume_index = 0; volume_index < volume_count;
             ++volume_index) {
            const double mean_pressure_pa_abs =
                pressure_sums[volume_index] / binary64_cycle_count;
            if (!std::isfinite(mean_pressure_pa_abs)) {
                FixedHorizonCycleSamplingError error;
                error.code = FixedHorizonCycleSamplingErrorCode::nonfinite_result;
                error.element_index = volume_index;
                return fail_finalization(std::move(error));
            }
            mean_pressures.push_back({
                plan_.physical_gas_volume_ids[volume_index],
                mean_pressure_pa_abs,
            });
        }

        const auto &first = retained_cycles_.front();
        const auto &last = retained_cycles_.back();
        terminal_result_ = FixedHorizonCycleSampled{{
            plan_.method,
            plan_.trailing_complete_cycle_count,
            plan_.fixed_preparation_horizon_s,
            {
                {
                    first.completed_cycle_ordinal,
                    last.completed_cycle_ordinal,
                    first.start_boundary,
                    last.end_boundary,
                },
                std::move(completed_cycles),
                indicated_gas_work_j,
                positive_aggregate_loss_work_j,
                starter_work_j,
                brake_work_j,
                mean_brake_torque_nm,
                std::move(mean_pressures),
            },
            last.completed_cycle_ordinal,
            last.end_boundary,
        }};
        return *terminal_result_;
    } catch (const std::bad_alloc &) {
        return fail_finalization(
            capacity_error(retained_cycles_.size(), retained_cycle_capacity_));
    } catch (const std::length_error &) {
        return fail_finalization(
            capacity_error(retained_cycles_.size(), retained_cycle_capacity_));
    }
}

std::size_t FixedHorizonCycleSampler::retained_eligible_cycle_count() const noexcept {
    return retained_cycles_.size();
}

std::size_t FixedHorizonCycleSampler::retained_cycle_capacity() const noexcept {
    return retained_cycle_capacity_;
}

bool FixedHorizonCycleSampler::finalized() const noexcept {
    return terminal_result_.has_value() || terminal_error_.has_value();
}

bool FixedHorizonCycleSampler::faulted() const noexcept {
    return terminal_error_.has_value();
}

FixedHorizonCycleSamplerCompileResult
compile_fixed_horizon_cycle_sampler(FixedHorizonCycleSamplerPlan plan) {
    if (plan.method != fixed_horizon_cycle_sampling_method_identity()) {
        return plan_error(
            FixedHorizonCycleSamplingPlanIssue::unsupported_method_identity);
    }
    if (plan.trailing_complete_cycle_count == 0U) {
        return plan_error(FixedHorizonCycleSamplingPlanIssue::invalid_cycle_count);
    }
    if (!std::isfinite(plan.fixed_preparation_horizon_s) ||
        !(plan.fixed_preparation_horizon_s > 0.0)) {
        return plan_error(FixedHorizonCycleSamplingPlanIssue::invalid_fixed_horizon);
    }
    if (plan.physical_gas_volume_ids.empty()) {
        return plan_error(FixedHorizonCycleSamplingPlanIssue::empty_pressure_inventory);
    }
    contract::GasVolumeId previous_id;
    for (std::size_t index = 0; index < plan.physical_gas_volume_ids.size(); ++index) {
        const auto id = plan.physical_gas_volume_ids[index];
        if (!id.valid()) {
            auto error = plan_error(
                FixedHorizonCycleSamplingPlanIssue::invalid_pressure_identity);
            error.element_index = index;
            return error;
        }
        if (index != 0 && id <= previous_id) {
            auto error = plan_error(
                FixedHorizonCycleSamplingPlanIssue::unstable_pressure_identity_order);
            error.element_index = index;
            return error;
        }
        previous_id = id;
    }

    if (static_cast<std::uint64_t>(plan.trailing_complete_cycle_count) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return capacity_error();
    }
    const std::size_t retained_cycle_capacity =
        static_cast<std::size_t>(plan.trailing_complete_cycle_count);
    if (plan.physical_gas_volume_ids.size() >
        std::numeric_limits<std::size_t>::max() / retained_cycle_capacity) {
        return capacity_error(0, retained_cycle_capacity);
    }

    try {
        return FixedHorizonCycleSampler{
            std::move(plan),
            retained_cycle_capacity,
        };
    } catch (const std::bad_alloc &) {
        return capacity_error(0, retained_cycle_capacity);
    } catch (const std::length_error &) {
        return capacity_error(0, retained_cycle_capacity);
    }
}

} // namespace crankwave::simulation
