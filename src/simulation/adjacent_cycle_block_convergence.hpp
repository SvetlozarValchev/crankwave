#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "simulation/four_stroke_cycle_integrator.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

inline constexpr std::string_view kAdjacentCycleBlockMeanConvergenceMethodId =
    "adjacent-nonoverlapping-cycle-block-mean-v1";
inline constexpr std::uint32_t kAdjacentCycleBlockMeanConvergenceMethodVersion = 1;

[[nodiscard]] std::string_view
adjacent_cycle_block_mean_convergence_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &
adjacent_cycle_block_mean_convergence_method_identity();

struct AdjacentCycleBlockBoundary {
    CycleBoundaryEvidence interpolation;
    double theta_rad = 0.0;
    double time_s = 0.0;

    friend bool operator==(const AdjacentCycleBlockBoundary &,
                           const AdjacentCycleBlockBoundary &) = default;
};

struct AdjacentCycleBlockPressure {
    contract::GasVolumeId gas_volume_id;
    double pressure_pa_abs = 0.0;

    friend bool operator==(const AdjacentCycleBlockPressure &,
                           const AdjacentCycleBlockPressure &) = default;
};

// This record owns one complete-cycle observation. Pressures are the exact
// end-boundary Poincare state in ascending stable GasVolumeId order.
struct AdjacentCycleBlockCycle {
    std::uint64_t completed_cycle_ordinal = 0;
    AdjacentCycleBlockBoundary start_boundary;
    AdjacentCycleBlockBoundary end_boundary;
    double indicated_gas_work_j = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    std::vector<AdjacentCycleBlockPressure> end_boundary_pressures;

    friend bool operator==(const AdjacentCycleBlockCycle &,
                           const AdjacentCycleBlockCycle &) = default;
};

struct AdjacentCycleBlockConvergencePlan {
    contract::MethodIdentity method;
    std::uint32_t cycles_per_block = 0;
    double eligibility_threshold_time_s = 0.0;
    double fixed_cutoff_time_s = 0.0;
    double cycle_mean_torque_tolerance_nm = 0.0;
    double pressure_tolerance_pa = 0.0;
    std::vector<contract::GasVolumeId> physical_gas_volume_ids;

    friend bool operator==(const AdjacentCycleBlockConvergencePlan &,
                           const AdjacentCycleBlockConvergencePlan &) = default;
};

struct AdjacentCycleBlockRange {
    std::uint64_t first_cycle_ordinal = 0;
    std::uint64_t last_cycle_ordinal = 0;
    AdjacentCycleBlockBoundary start_boundary;
    AdjacentCycleBlockBoundary end_boundary;

    friend bool operator==(const AdjacentCycleBlockRange &,
                           const AdjacentCycleBlockRange &) = default;
};

struct AdjacentCycleBlockMean {
    AdjacentCycleBlockRange range;
    double total_indicated_gas_work_j = 0.0;
    double total_positive_aggregate_loss_work_j = 0.0;
    double total_starter_work_j = 0.0;
    double total_brake_work_j = 0.0;
    double mean_brake_torque_nm = 0.0;

    friend bool operator==(const AdjacentCycleBlockMean &,
                           const AdjacentCycleBlockMean &) = default;
};

struct AdjacentCycleBlockPressureMeans {
    contract::GasVolumeId gas_volume_id;
    double block_a_mean_pressure_pa_abs = 0.0;
    double block_b_mean_pressure_pa_abs = 0.0;

    friend bool operator==(const AdjacentCycleBlockPressureMeans &,
                           const AdjacentCycleBlockPressureMeans &) = default;
};

struct AdjacentCycleBlockConvergenceEvidence {
    contract::MethodIdentity method;
    std::uint32_t cycles_per_block = 0;
    double eligibility_threshold_time_s = 0.0;
    double fixed_cutoff_time_s = 0.0;
    AdjacentCycleBlockMean block_a;
    AdjacentCycleBlockMean block_b;
    double torque_residual_nm = 0.0;
    double cycle_mean_torque_tolerance_nm = 0.0;
    std::vector<AdjacentCycleBlockPressureMeans> pressure_means;
    double pressure_residual_pa = 0.0;
    contract::GasVolumeId limiting_gas_volume_id;
    double pressure_tolerance_pa = 0.0;
    bool settled = false;

    friend bool operator==(const AdjacentCycleBlockConvergenceEvidence &,
                           const AdjacentCycleBlockConvergenceEvidence &) = default;
};

struct AdjacentCycleBlockConverged {
    AdjacentCycleBlockConvergenceEvidence evidence;

    friend bool operator==(const AdjacentCycleBlockConverged &,
                           const AdjacentCycleBlockConverged &) = default;
};

enum class AdjacentCycleBlockConvergenceErrorCode : std::uint8_t {
    invalid_plan,
    capacity_overflow,
    malformed_cycle_input,
    nonfinite_result,
    insufficient_cycles,
    nonconverged,
};

enum class AdjacentCycleBlockConvergencePlanIssue : std::uint8_t {
    none,
    unsupported_method_identity,
    invalid_cycle_count,
    invalid_time_bounds,
    invalid_torque_tolerance,
    invalid_pressure_tolerance,
    empty_pressure_inventory,
    invalid_pressure_identity,
    unstable_pressure_identity_order,
};

enum class AdjacentCycleBlockConvergenceInputIssue : std::uint8_t {
    none,
    invalid_boundary_evidence,
    nonfinite_cycle_scalar,
    nonpositive_cycle_extent,
    nonfinite_cycle_work,
    nonpositive_aggregate_loss_work,
    noncanonical_starter_work,
    incoherent_brake_work,
    noncontiguous_cycle_ordinal,
    noncontiguous_cycle_boundary,
    pressure_shape_mismatch,
    invalid_pressure_identity,
    unstable_pressure_identity_order,
    unexpected_pressure_identity,
    nonfinite_pressure,
    nonpositive_pressure,
};

inline constexpr std::size_t kNoAdjacentCycleBlockElement =
    std::numeric_limits<std::size_t>::max();

struct AdjacentCycleBlockConvergenceError {
    AdjacentCycleBlockConvergenceErrorCode code =
        AdjacentCycleBlockConvergenceErrorCode::invalid_plan;
    AdjacentCycleBlockConvergencePlanIssue plan_issue =
        AdjacentCycleBlockConvergencePlanIssue::none;
    AdjacentCycleBlockConvergenceInputIssue input_issue =
        AdjacentCycleBlockConvergenceInputIssue::none;
    std::uint64_t cycle_ordinal = 0;
    std::size_t element_index = kNoAdjacentCycleBlockElement;
    std::size_t retained_cycle_count = 0;
    std::size_t required_cycle_count = 0;
    std::optional<AdjacentCycleBlockConvergenceEvidence> evidence;

    friend bool operator==(const AdjacentCycleBlockConvergenceError &,
                           const AdjacentCycleBlockConvergenceError &) = default;
};

struct AdjacentCycleBlockObservationAccepted {
    bool eligible = false;
    std::size_t retained_eligible_cycle_count = 0;

    friend bool operator==(const AdjacentCycleBlockObservationAccepted &,
                           const AdjacentCycleBlockObservationAccepted &) = default;
};

struct AdjacentCycleBlockObservationClosed {
    friend bool operator==(const AdjacentCycleBlockObservationClosed &,
                           const AdjacentCycleBlockObservationClosed &) = default;
};

using AdjacentCycleBlockObservationResult =
    std::variant<AdjacentCycleBlockObservationAccepted,
                 AdjacentCycleBlockObservationClosed,
                 AdjacentCycleBlockConvergenceError>;
using AdjacentCycleBlockFinalizationResult =
    std::variant<AdjacentCycleBlockConverged, AdjacentCycleBlockConvergenceError>;

class AdjacentCycleBlockConvergenceObserver final {
  public:
    AdjacentCycleBlockConvergenceObserver(
        const AdjacentCycleBlockConvergenceObserver &) = delete;
    AdjacentCycleBlockConvergenceObserver &
    operator=(const AdjacentCycleBlockConvergenceObserver &) = delete;
    AdjacentCycleBlockConvergenceObserver(
        AdjacentCycleBlockConvergenceObserver &&) noexcept = default;
    AdjacentCycleBlockConvergenceObserver &
    operator=(AdjacentCycleBlockConvergenceObserver &&) noexcept = default;

    // Observing never reports convergence. The fixed-window result is evaluated
    // only by finalize_at_fixed_cutoff(), after the caller reaches the compiled
    // cutoff. Malformed input is terminal and stable.
    [[nodiscard]] AdjacentCycleBlockObservationResult
    observe(const AdjacentCycleBlockCycle &cycle);
    [[nodiscard]] AdjacentCycleBlockFinalizationResult finalize_at_fixed_cutoff();

    [[nodiscard]] std::size_t retained_eligible_cycle_count() const noexcept;
    [[nodiscard]] std::size_t retained_cycle_capacity() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool faulted() const noexcept;

  private:
    struct StoredCycle {
        std::uint64_t completed_cycle_ordinal = 0;
        AdjacentCycleBlockBoundary start_boundary;
        AdjacentCycleBlockBoundary end_boundary;
        double indicated_gas_work_j = 0.0;
        double positive_aggregate_loss_work_j = 0.0;
        double starter_work_j = 0.0;
        double brake_work_j = 0.0;
        std::vector<double> end_boundary_pressures_pa_abs;
    };

    AdjacentCycleBlockConvergenceObserver(AdjacentCycleBlockConvergencePlan plan,
                                          std::size_t retained_cycle_capacity);

    [[nodiscard]] AdjacentCycleBlockConvergenceError
    malformed(AdjacentCycleBlockConvergenceInputIssue issue,
              const AdjacentCycleBlockCycle &cycle,
              std::size_t element_index = kNoAdjacentCycleBlockElement);
    [[nodiscard]] AdjacentCycleBlockObservationResult
    fail_observation(AdjacentCycleBlockConvergenceError error);
    [[nodiscard]] AdjacentCycleBlockFinalizationResult
    fail_finalization(AdjacentCycleBlockConvergenceError error);

    AdjacentCycleBlockConvergencePlan plan_;
    std::size_t retained_cycle_capacity_ = 0;
    std::deque<StoredCycle> retained_cycles_;
    std::optional<StoredCycle> previous_cycle_;
    std::optional<AdjacentCycleBlockConverged> terminal_result_;
    std::optional<AdjacentCycleBlockConvergenceError> terminal_error_;

    friend std::variant<AdjacentCycleBlockConvergenceObserver,
                        AdjacentCycleBlockConvergenceError>
        compile_adjacent_cycle_block_convergence_observer(
            AdjacentCycleBlockConvergencePlan);
};

using AdjacentCycleBlockConvergenceCompileResult =
    std::variant<AdjacentCycleBlockConvergenceObserver,
                 AdjacentCycleBlockConvergenceError>;

[[nodiscard]] AdjacentCycleBlockConvergenceCompileResult
compile_adjacent_cycle_block_convergence_observer(
    AdjacentCycleBlockConvergencePlan plan);

} // namespace engine_sim_offline::simulation
