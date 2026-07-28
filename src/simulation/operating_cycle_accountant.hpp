#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/torque.hpp"
#include "simulation/chen_flynn_cycle_mean_loss.hpp"
#include "simulation/four_stroke_cycle_integrator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct OperatingCylinderAccountingPlan {
    contract::CylinderId cylinder_id;
    contract::GasVolumeId chamber_volume_id;
    double displacement_m3 = 0.0;

    friend bool operator==(const OperatingCylinderAccountingPlan &,
                           const OperatingCylinderAccountingPlan &) = default;
};

struct OperatingCycleAccountingPlan {
    FourStrokeCycleIntegrationPlan quadrature;
    ChenFlynnCycleMeanLossPlan aggregate_loss;
    double engine_speed_rpm = 0.0;
    double stroke_m = 0.0;
    bool starter_mechanically_disengaged = false;
    contract::TorqueTermMask indicated_terms = 0;
    contract::TorqueTermMask aggregate_loss_terms = 0;
    contract::TorqueTermMask starter_terms = 0;
    std::vector<OperatingCylinderAccountingPlan> cylinders;
    std::vector<contract::GasVolumeId> physically_resolved_gas_volumes;

    friend bool operator==(const OperatingCycleAccountingPlan &,
                           const OperatingCycleAccountingPlan &) = default;
};

struct OperatingGasVolumePressureSample {
    contract::GasVolumeId gas_volume_id;
    double pressure_pa_abs = 0.0;

    friend bool operator==(const OperatingGasVolumePressureSample &,
                           const OperatingGasVolumePressureSample &) = default;
};

struct OperatingCycleSample {
    std::uint64_t sample_index = 0;
    double time_s = 0.0;
    double theta_unwrapped_rad = 0.0;
    double engine_speed_rpm = 0.0;
    double indicated_gas_torque_nm = 0.0;
    std::span<const OperatingGasVolumePressureSample> physically_resolved_gas_volumes;
};

struct OperatingCylinderPeakPressure {
    contract::CylinderId cylinder_id;
    contract::GasVolumeId chamber_volume_id;
    double displacement_m3 = 0.0;
    double peak_pressure_pa_abs = 0.0;

    friend bool operator==(const OperatingCylinderPeakPressure &,
                           const OperatingCylinderPeakPressure &) = default;
};

struct OperatingCompletedCycle {
    // The generic quadrature was supplied indicated gas and canonical +0.0
    // placeholders. Its "summed" fields therefore remain indicated-only evidence
    // and are never promoted as brake/net values.
    CompletedFourStrokeCycle indicated_quadrature;
    std::vector<OperatingCylinderPeakPressure> cylinder_peak_pressures;
    ChenFlynnCycleMeanLossResult aggregate_loss;
    double starter_work_j = 0.0;
    double brake_work_j = 0.0;
    double cycle_mean_brake_torque_nm = 0.0;
    double net_brake_mean_effective_pressure_pa = 0.0;
    double cycle_mean_brake_power_w = 0.0;

    friend bool operator==(const OperatingCompletedCycle &,
                           const OperatingCompletedCycle &) = default;
};

struct NoOperatingCycleBoundaryCrossing {
    friend bool operator==(const NoOperatingCycleBoundaryCrossing &,
                           const NoOperatingCycleBoundaryCrossing &) = default;
};

struct OperatingCycleBoundaryCrossing {
    CycleBoundaryEvidence boundary;
    double theta_rad = 0.0;
    double time_s = 0.0;
    std::vector<OperatingGasVolumePressureSample> boundary_pressures;
    std::optional<OperatingCompletedCycle> completed_cycle;

    friend bool operator==(const OperatingCycleBoundaryCrossing &,
                           const OperatingCycleBoundaryCrossing &) = default;
};

enum class OperatingCycleAccountingErrorCode : std::uint8_t {
    invalid_plan,
    invalid_term_partition,
    invalid_cylinder_plan,
    invalid_gas_volume_plan,
    malformed_sample,
    nonfinite_pressure,
    nonpositive_pressure,
    engine_speed_mismatch,
    quadrature_failure,
    nonzero_placeholder_work,
    aggregate_loss_failure,
    accounting_invariant_violation,
    nonfinite_result,
    moved_from,
};

inline constexpr std::size_t kNoOperatingCycleAccountingElement =
    std::numeric_limits<std::size_t>::max();

struct OperatingCycleAccountingError {
    OperatingCycleAccountingErrorCode code =
        OperatingCycleAccountingErrorCode::invalid_plan;
    std::uint64_t sample_index = 0;
    std::size_t element_index = kNoOperatingCycleAccountingElement;
    std::optional<FourStrokeCycleIntegrationErrorCode> quadrature_error;
    std::optional<ChenFlynnCycleMeanLossErrorCode> aggregate_loss_error;

    friend bool operator==(const OperatingCycleAccountingError &,
                           const OperatingCycleAccountingError &) = default;
};

using OperatingCycleAccountingAdvanceResult =
    std::variant<NoOperatingCycleBoundaryCrossing, OperatingCycleBoundaryCrossing,
                 OperatingCycleAccountingError>;

class OperatingCycleAccountant final {
  public:
    OperatingCycleAccountant(const OperatingCycleAccountant &) = delete;
    OperatingCycleAccountant &operator=(const OperatingCycleAccountant &) = delete;
    OperatingCycleAccountant(OperatingCycleAccountant &&other) noexcept;
    OperatingCycleAccountant &operator=(OperatingCycleAccountant &&other) noexcept;

    // Pressure vectors must exactly match the compiled stable identities. The
    // initial partial cycle is observed but discarded by the shared quadrature.
    // Every returned boundary pressure uses that quadrature's bracket and fraction.
    [[nodiscard]] OperatingCycleAccountingAdvanceResult
    advance(const OperatingCycleSample &sample);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] std::uint64_t completed_cycle_count() const noexcept;

  private:
    OperatingCycleAccountant(OperatingCycleAccountingPlan plan,
                             FourStrokeCycleIntegrator quadrature,
                             std::vector<std::size_t> cylinder_volume_indices);

    [[nodiscard]] OperatingCycleAccountingAdvanceResult
    fail(OperatingCycleAccountingErrorCode code, std::uint64_t sample_index,
         std::size_t element_index = kNoOperatingCycleAccountingElement,
         std::optional<FourStrokeCycleIntegrationErrorCode> quadrature_error =
             std::nullopt,
         std::optional<ChenFlynnCycleMeanLossErrorCode> aggregate_loss_error =
             std::nullopt);
    [[nodiscard]] std::optional<OperatingCycleAccountingError>
    validate_sample(const OperatingCycleSample &sample) const noexcept;
    void seed_cycle_peaks(
        std::span<const OperatingGasVolumePressureSample> gas_volumes) noexcept;
    void retain_cycle_peaks(
        std::span<const OperatingGasVolumePressureSample> gas_volumes) noexcept;
    void invalidate_after_move() noexcept;

    OperatingCycleAccountingPlan plan_;
    FourStrokeCycleIntegrator quadrature_;
    std::vector<std::size_t> cylinder_volume_indices_;
    std::vector<OperatingGasVolumePressureSample> previous_gas_volumes_;
    std::vector<double> current_cycle_peak_pressures_pa_abs_;
    std::uint64_t completed_cycle_count_ = 0;
    std::optional<OperatingCycleAccountingError> terminal_error_;

    friend std::variant<OperatingCycleAccountant, OperatingCycleAccountingError>
        compile_operating_cycle_accountant(OperatingCycleAccountingPlan);
};

using OperatingCycleAccountantCompileResult =
    std::variant<OperatingCycleAccountant, OperatingCycleAccountingError>;

// A compiled accountant is the authority that proves the complete 0xff cycle-mean
// partition. It accepts indicated gas 0x01, aggregate loss 0x7e, and a mechanically
// disengaged starter 0x80 only; no caller-provided alternative partition is promoted.
[[nodiscard]] OperatingCycleAccountantCompileResult
compile_operating_cycle_accountant(OperatingCycleAccountingPlan plan);

} // namespace engine_sim_offline::simulation
