#pragma once

#include <cstdint>
#include <optional>
#include <variant>

namespace engine_sim_offline::simulation {

struct FourStrokeCycleIntegrationPlan {
    double cycle_reference_theta_rad = 0.0;
    double total_displacement_m3 = 0.0;

    friend bool operator==(const FourStrokeCycleIntegrationPlan &,
                           const FourStrokeCycleIntegrationPlan &) = default;
};

struct CycleTorqueSample {
    std::uint64_t sample_index = 0;
    double time_s = 0.0;
    double theta_unwrapped_rad = 0.0;
    double indicated_gas_torque_nm = 0.0;
    double friction_pump_and_accessory_torque_nm = 0.0;
    double starter_torque_nm = 0.0;

    friend bool operator==(const CycleTorqueSample &,
                           const CycleTorqueSample &) = default;
};

struct CycleBoundaryEvidence {
    std::uint64_t left_bracket_sample_index = 0;
    std::uint64_t right_bracket_sample_index = 0;
    double fraction_from_left_01 = 0.0;

    friend bool operator==(const CycleBoundaryEvidence &,
                           const CycleBoundaryEvidence &) = default;
};

struct CompletedFourStrokeCycle {
    std::uint64_t completed_cycle_ordinal = 0;
    CycleBoundaryEvidence start_boundary;
    CycleBoundaryEvidence end_boundary;
    double start_theta_rad = 0.0;
    double end_theta_rad = 0.0;
    double start_time_s = 0.0;
    double end_time_s = 0.0;
    double indicated_gas_work_j = 0.0;
    double friction_pump_and_accessory_work_j = 0.0;
    double starter_work_j = 0.0;
    double summed_torque_work_j = 0.0;
    double cycle_mean_summed_torque_nm = 0.0;
    double summed_torque_mean_effective_pressure_pa = 0.0;
    double cycle_mean_summed_power_w = 0.0;

    friend bool operator==(const CompletedFourStrokeCycle &,
                           const CompletedFourStrokeCycle &) = default;
};

enum class FourStrokeCycleIntegrationErrorCode : std::uint8_t {
    invalid_plan,
    nonfinite_sample,
    nonmonotonic_sample,
    multiple_boundaries_in_segment,
    nonfinite_result,
    moved_from,
};

struct FourStrokeCycleIntegrationError {
    FourStrokeCycleIntegrationErrorCode code =
        FourStrokeCycleIntegrationErrorCode::invalid_plan;
    std::uint64_t sample_index = 0;

    friend bool operator==(const FourStrokeCycleIntegrationError &,
                           const FourStrokeCycleIntegrationError &) = default;
};

struct NoCompletedFourStrokeCycle {
    friend bool operator==(const NoCompletedFourStrokeCycle &,
                           const NoCompletedFourStrokeCycle &) = default;
};

using FourStrokeCycleAdvanceResult =
    std::variant<NoCompletedFourStrokeCycle, CompletedFourStrokeCycle,
                 FourStrokeCycleIntegrationError>;

class FourStrokeCycleIntegrator final {
  public:
    FourStrokeCycleIntegrator(const FourStrokeCycleIntegrator &) = delete;
    FourStrokeCycleIntegrator &operator=(const FourStrokeCycleIntegrator &) = delete;
    FourStrokeCycleIntegrator(FourStrokeCycleIntegrator &&other) noexcept;
    FourStrokeCycleIntegrator &operator=(FourStrokeCycleIntegrator &&other) noexcept;

    // Samples use strictly increasing post-step time/angle semantics. Between samples,
    // torque and time are piecewise linear in unwrapped crank angle; work uses
    // trapezoidal torque-angle quadrature. Boundaries are derived independently as
    // reference + integer*4*pi, and the initial partial cycle is discarded. Upstream
    // integration must split physical discontinuities at their exact event times.
    //
    // This primitive integrates supplied terms but makes no claim that they constitute
    // complete physical net torque; that proof belongs to the compiled
    // torque-accounting source upstream. Completion and failure are stable and
    // session-local.
    [[nodiscard]] FourStrokeCycleAdvanceResult
    advance(const CycleTorqueSample &sample) noexcept;

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] std::uint64_t completed_cycle_count() const noexcept;

  private:
    struct TorquePoint {
        std::uint64_t sample_index = 0;
        double time_s = 0.0;
        double theta_rad = 0.0;
        double indicated_gas_torque_nm = 0.0;
        double friction_pump_and_accessory_torque_nm = 0.0;
        double starter_torque_nm = 0.0;
        double summed_torque_nm = 0.0;
        CycleBoundaryEvidence boundary_evidence;
    };

    struct WorkAccumulator {
        double indicated_gas_work_j = 0.0;
        double friction_pump_and_accessory_work_j = 0.0;
        double starter_work_j = 0.0;
        double summed_torque_work_j = 0.0;
    };

    FourStrokeCycleIntegrator(double cycle_reference_theta_rad,
                              double total_displacement_m3) noexcept;

    [[nodiscard]] FourStrokeCycleAdvanceResult
    fail(FourStrokeCycleIntegrationErrorCode code, std::uint64_t sample_index) noexcept;
    [[nodiscard]] TorquePoint
    interpolate_boundary(const TorquePoint &left, const TorquePoint &right,
                         double boundary_theta_rad) const noexcept;
    void integrate_segment(const TorquePoint &left, const TorquePoint &right) noexcept;
    void begin_cycle(const TorquePoint &boundary) noexcept;
    [[nodiscard]] CompletedFourStrokeCycle
    finish_cycle(const TorquePoint &boundary) noexcept;
    [[nodiscard]] double boundary_theta(std::int64_t cycle_index) const noexcept;
    void invalidate_after_move() noexcept;

    double cycle_reference_theta_rad_ = 0.0;
    double total_displacement_m3_ = 0.0;
    double next_boundary_theta_rad_ = 0.0;
    std::int64_t next_boundary_cycle_index_ = 0;
    double cycle_start_theta_rad_ = 0.0;
    double cycle_start_time_s_ = 0.0;
    CycleBoundaryEvidence cycle_start_boundary_;
    std::uint64_t completed_cycle_count_ = 0;
    bool accumulating_full_cycle_ = false;
    std::optional<TorquePoint> previous_;
    WorkAccumulator work_;
    std::optional<FourStrokeCycleIntegrationError> terminal_error_;

    friend std::variant<FourStrokeCycleIntegrator, FourStrokeCycleIntegrationError>
    compile_four_stroke_cycle_integrator(
        const FourStrokeCycleIntegrationPlan &) noexcept;
};

using FourStrokeCycleIntegratorCompileResult =
    std::variant<FourStrokeCycleIntegrator, FourStrokeCycleIntegrationError>;

[[nodiscard]] FourStrokeCycleIntegratorCompileResult
compile_four_stroke_cycle_integrator(
    const FourStrokeCycleIntegrationPlan &plan) noexcept;

} // namespace engine_sim_offline::simulation
