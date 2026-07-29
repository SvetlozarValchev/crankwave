#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
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

[[nodiscard]] std::string_view
fixed_horizon_cycle_sampling_method_descriptor() noexcept;
[[nodiscard]] const contract::MethodIdentity &
fixed_horizon_cycle_sampling_method_identity();

struct FixedHorizonCycleBoundary {
    CycleBoundaryEvidence interpolation;
    double theta_rad = 0.0;
    double time_s = 0.0;

    friend bool operator==(const FixedHorizonCycleBoundary &,
                           const FixedHorizonCycleBoundary &) = default;
};

struct FixedHorizonCyclePressure {
    contract::GasVolumeId gas_volume_id;
    double pressure_pa_abs = 0.0;

    friend bool operator==(const FixedHorizonCyclePressure &,
                           const FixedHorizonCyclePressure &) = default;
};

// One owned complete-cycle observation. End-boundary pressures use the exact
// ascending physical-volume inventory compiled into the sampler plan.
struct FixedHorizonCycle {
    std::uint64_t completed_cycle_ordinal = 0;
    FixedHorizonCycleBoundary start_boundary;
    FixedHorizonCycleBoundary end_boundary;
    double indicated_gas_work_j = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    std::vector<FixedHorizonCyclePressure> end_boundary_pressures;

    friend bool operator==(const FixedHorizonCycle &,
                           const FixedHorizonCycle &) = default;
};

struct FixedHorizonCycleSamplerPlan {
    contract::MethodIdentity method;
    std::uint32_t trailing_complete_cycle_count = 0;
    double fixed_preparation_horizon_s = 0.0;
    std::vector<contract::GasVolumeId> physical_gas_volume_ids;

    friend bool operator==(const FixedHorizonCycleSamplerPlan &,
                           const FixedHorizonCycleSamplerPlan &) = default;
};

struct FixedHorizonCycleRange {
    std::uint64_t first_cycle_ordinal = 0;
    std::uint64_t last_cycle_ordinal = 0;
    FixedHorizonCycleBoundary start_boundary;
    FixedHorizonCycleBoundary end_boundary;

    friend bool operator==(const FixedHorizonCycleRange &,
                           const FixedHorizonCycleRange &) = default;
};

struct FixedHorizonCompletedCycle {
    std::uint64_t completed_cycle_ordinal = 0;
    double indicated_gas_work_j = 0.0;
    double positive_aggregate_loss_work_j = 0.0;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    std::vector<FixedHorizonCyclePressure> end_boundary_pressures;

    friend bool operator==(const FixedHorizonCompletedCycle &,
                           const FixedHorizonCompletedCycle &) = default;
};

struct FixedHorizonMeanBoundaryPressure {
    contract::GasVolumeId gas_volume_id;
    double mean_pressure_pa_abs = 0.0;

    friend bool operator==(const FixedHorizonMeanBoundaryPressure &,
                           const FixedHorizonMeanBoundaryPressure &) = default;
};

struct FixedHorizonCycleSample {
    FixedHorizonCycleRange range;
    std::vector<FixedHorizonCompletedCycle> completed_cycles;
    double total_indicated_gas_work_j = 0.0;
    double total_positive_aggregate_loss_work_j = 0.0;
    double total_starter_work_j = 0.0;
    double total_brake_work_j = 0.0;
    double mean_brake_torque_nm = 0.0;
    std::vector<FixedHorizonMeanBoundaryPressure> mean_boundary_pressures;

    friend bool operator==(const FixedHorizonCycleSample &,
                           const FixedHorizonCycleSample &) = default;
};

struct FixedHorizonCycleSamplingEvidence {
    contract::MethodIdentity method;
    std::uint32_t trailing_complete_cycle_count = 0;
    double fixed_preparation_horizon_s = 0.0;
    FixedHorizonCycleSample trailing_complete_cycles;
    std::uint64_t last_eligible_completed_cycle_ordinal_at_fixed_horizon = 0;
    FixedHorizonCycleBoundary last_eligible_cycle_end_boundary_at_fixed_horizon;

    friend bool operator==(const FixedHorizonCycleSamplingEvidence &,
                           const FixedHorizonCycleSamplingEvidence &) = default;
};

struct FixedHorizonCycleSampled {
    FixedHorizonCycleSamplingEvidence evidence;

    friend bool operator==(const FixedHorizonCycleSampled &,
                           const FixedHorizonCycleSampled &) = default;
};

enum class FixedHorizonCycleSamplingErrorCode : std::uint8_t {
    invalid_plan,
    capacity_overflow,
    malformed_cycle_input,
    nonfinite_result,
    insufficient_cycles,
};

enum class FixedHorizonCycleSamplingPlanIssue : std::uint8_t {
    none,
    unsupported_method_identity,
    invalid_cycle_count,
    invalid_fixed_horizon,
    empty_pressure_inventory,
    invalid_pressure_identity,
    unstable_pressure_identity_order,
};

enum class FixedHorizonCycleSamplingInputIssue : std::uint8_t {
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

inline constexpr std::size_t kNoFixedHorizonCycleSamplingElement =
    std::numeric_limits<std::size_t>::max();

struct FixedHorizonCycleSamplingError {
    FixedHorizonCycleSamplingErrorCode code =
        FixedHorizonCycleSamplingErrorCode::invalid_plan;
    FixedHorizonCycleSamplingPlanIssue plan_issue =
        FixedHorizonCycleSamplingPlanIssue::none;
    FixedHorizonCycleSamplingInputIssue input_issue =
        FixedHorizonCycleSamplingInputIssue::none;
    std::uint64_t cycle_ordinal = 0;
    std::size_t element_index = kNoFixedHorizonCycleSamplingElement;
    std::size_t retained_cycle_count = 0;
    std::size_t required_cycle_count = 0;

    friend bool operator==(const FixedHorizonCycleSamplingError &,
                           const FixedHorizonCycleSamplingError &) = default;
};

struct FixedHorizonCycleObservationAccepted {
    bool eligible = false;
    std::size_t retained_eligible_cycle_count = 0;

    friend bool operator==(const FixedHorizonCycleObservationAccepted &,
                           const FixedHorizonCycleObservationAccepted &) = default;
};

struct FixedHorizonCycleObservationClosed {
    friend bool operator==(const FixedHorizonCycleObservationClosed &,
                           const FixedHorizonCycleObservationClosed &) = default;
};

using FixedHorizonCycleObservationResult =
    std::variant<FixedHorizonCycleObservationAccepted,
                 FixedHorizonCycleObservationClosed, FixedHorizonCycleSamplingError>;
using FixedHorizonCycleFinalizationResult =
    std::variant<FixedHorizonCycleSampled, FixedHorizonCycleSamplingError>;

class FixedHorizonCycleSampler final {
  public:
    FixedHorizonCycleSampler(const FixedHorizonCycleSampler &) = delete;
    FixedHorizonCycleSampler &operator=(const FixedHorizonCycleSampler &) = delete;
    FixedHorizonCycleSampler(FixedHorizonCycleSampler &&) noexcept = default;
    FixedHorizonCycleSampler &operator=(FixedHorizonCycleSampler &&) noexcept = default;

    // Observation only validates chronology and updates the bounded latest-cycle
    // ring. The sample is produced only by explicit fixed-horizon finalization.
    [[nodiscard]] FixedHorizonCycleObservationResult
    observe(const FixedHorizonCycle &cycle);
    [[nodiscard]] FixedHorizonCycleFinalizationResult finalize_at_fixed_horizon();

    [[nodiscard]] std::size_t retained_eligible_cycle_count() const noexcept;
    [[nodiscard]] std::size_t retained_cycle_capacity() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool faulted() const noexcept;

  private:
    struct StoredCycle {
        std::uint64_t completed_cycle_ordinal = 0;
        FixedHorizonCycleBoundary start_boundary;
        FixedHorizonCycleBoundary end_boundary;
        double indicated_gas_work_j = 0.0;
        double positive_aggregate_loss_work_j = 0.0;
        double starter_work_j = 0.0;
        double brake_work_j = 0.0;
        std::vector<double> end_boundary_pressures_pa_abs;
    };

    FixedHorizonCycleSampler(FixedHorizonCycleSamplerPlan plan,
                             std::size_t retained_cycle_capacity);

    [[nodiscard]] FixedHorizonCycleSamplingError
    malformed(FixedHorizonCycleSamplingInputIssue issue, const FixedHorizonCycle &cycle,
              std::size_t element_index = kNoFixedHorizonCycleSamplingElement);
    [[nodiscard]] FixedHorizonCycleObservationResult
    fail_observation(FixedHorizonCycleSamplingError error);
    [[nodiscard]] FixedHorizonCycleFinalizationResult
    fail_finalization(FixedHorizonCycleSamplingError error);

    FixedHorizonCycleSamplerPlan plan_;
    std::size_t retained_cycle_capacity_ = 0;
    std::deque<StoredCycle> retained_cycles_;
    std::optional<StoredCycle> previous_cycle_;
    std::optional<FixedHorizonCycleSampled> terminal_result_;
    std::optional<FixedHorizonCycleSamplingError> terminal_error_;

    friend std::variant<FixedHorizonCycleSampler, FixedHorizonCycleSamplingError>
        compile_fixed_horizon_cycle_sampler(FixedHorizonCycleSamplerPlan);
};

using FixedHorizonCycleSamplerCompileResult =
    std::variant<FixedHorizonCycleSampler, FixedHorizonCycleSamplingError>;

[[nodiscard]] FixedHorizonCycleSamplerCompileResult
compile_fixed_horizon_cycle_sampler(FixedHorizonCycleSamplerPlan plan);

} // namespace engine_sim_offline::simulation
