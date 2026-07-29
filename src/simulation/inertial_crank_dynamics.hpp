#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct InertialCrankBrakePoint {
    double angular_speed_rad_s = 0.0;
    double resisting_torque_nm = 0.0;

    friend bool operator==(const InertialCrankBrakePoint &,
                           const InertialCrankBrakePoint &) = default;
};

struct InertialCrankDynamicsConfiguration {
    double equivalent_inertia_kg_m2 = 0.0;
    std::vector<InertialCrankBrakePoint> passive_brake_curve;

    friend bool operator==(const InertialCrankDynamicsConfiguration &,
                           const InertialCrankDynamicsConfiguration &) = default;
};

struct InertialCrankState {
    double theta_rad = 0.0;
    double angular_speed_rad_s = 0.0;

    friend bool operator==(const InertialCrankState &,
                           const InertialCrankState &) = default;
};

// held_total_crank_torque_nm is the causal torque sample upstream of the passive
// dyno brake. It can include signed indicated, loss, starter, and other explicitly
// accounted crank torques. Both that sample and the brake magnitude sampled from
// the curve at initial_state.angular_speed_rad_s are zero-order held for duration_s.
struct InertialCrankStepInput {
    InertialCrankState initial_state;
    double held_total_crank_torque_nm = 0.0;
    double duration_s = 0.0;

    friend bool operator==(const InertialCrankStepInput &,
                           const InertialCrankStepInput &) = default;
};

struct InertialCrankStepResult {
    InertialCrankState initial_state;
    InertialCrankState final_state;
    double held_total_crank_torque_nm = 0.0;
    // Positive magnitude acting opposite positive crank rotation.
    double applied_brake_torque_nm = 0.0;
    double held_net_torque_nm = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double angular_displacement_rad = 0.0;
    double kinetic_energy_change_j = 0.0;
    double held_net_torque_work_j = 0.0;
    // kinetic_energy_change_j - held_net_torque_work_j. A finite binary64
    // roundoff residual is exposed rather than silently discarded.
    double energy_residual_j = 0.0;

    friend bool operator==(const InertialCrankStepResult &,
                           const InertialCrankStepResult &) = default;
};

enum class InertialCrankDynamicsErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_input,
    brake_curve_out_of_domain,
    stall,
};

enum class InertialCrankConfigurationIssue : std::uint8_t {
    none,
    nonfinite_equivalent_inertia,
    nonpositive_equivalent_inertia,
    insufficient_brake_curve_points,
    nonfinite_brake_speed,
    negative_brake_speed,
    nonfinite_brake_torque,
    negative_brake_torque,
    unstable_brake_speed_order,
};

enum class InertialCrankInputIssue : std::uint8_t {
    none,
    nonfinite_theta,
    nonfinite_angular_speed,
    nonpositive_angular_speed,
    nonfinite_total_crank_torque,
    nonfinite_duration,
    nonpositive_duration,
    nonfinite_derived_value,
};

enum class InertialCrankDomainIssue : std::uint8_t {
    none,
    initial_speed_below_curve,
    initial_speed_above_curve,
    final_speed_below_curve,
    final_speed_above_curve,
};

inline constexpr std::size_t kNoInertialCrankBrakePoint =
    std::numeric_limits<std::size_t>::max();

struct InertialCrankDynamicsError {
    InertialCrankDynamicsErrorCode code =
        InertialCrankDynamicsErrorCode::invalid_configuration;
    InertialCrankConfigurationIssue configuration_issue =
        InertialCrankConfigurationIssue::none;
    InertialCrankInputIssue input_issue = InertialCrankInputIssue::none;
    InertialCrankDomainIssue domain_issue = InertialCrankDomainIssue::none;
    std::size_t brake_point_index = kNoInertialCrankBrakePoint;
    // The rejected initial or predicted final speed for domain/stall errors.
    double angular_speed_rad_s = 0.0;
    // For stall only, the resolved positive time from the beginning of the rejected
    // step and the corresponding last positive-rotation crank angle.
    double stall_time_s = 0.0;
    double stall_theta_rad = 0.0;

    friend bool operator==(const InertialCrankDynamicsError &,
                           const InertialCrankDynamicsError &) = default;
};

using InertialCrankStepCalculation =
    std::variant<InertialCrankStepResult, InertialCrankDynamicsError>;

class InertialCrankDynamics final {
  public:
    InertialCrankDynamics(const InertialCrankDynamics &) = default;
    InertialCrankDynamics(InertialCrankDynamics &&) noexcept = default;
    InertialCrankDynamics &operator=(const InertialCrankDynamics &) = default;
    InertialCrankDynamics &operator=(InertialCrankDynamics &&) noexcept = default;

    [[nodiscard]] double equivalent_inertia_kg_m2() const noexcept;
    [[nodiscard]] const std::vector<InertialCrankBrakePoint> &
    passive_brake_curve() const noexcept;

    // For the two zero-order-held torques, alpha is constant for this step:
    //   alpha  = (held_total_crank_torque - applied_brake_torque) / inertia
    //   omega1 = omega0 + alpha * dt
    //   theta1 = theta0 + omega0 * dt + 0.5 * alpha * dt^2
    // This constant-acceleration update makes net-torque work over theta1-theta0
    // equal the change in 0.5*inertia*omega^2, apart from exposed binary64
    // roundoff. The step is rejected rather than extrapolating the brake curve or
    // publishing a stalled/reversing state.
    [[nodiscard]] InertialCrankStepCalculation
    advance(const InertialCrankStepInput &input) const noexcept;

  private:
    explicit InertialCrankDynamics(
        InertialCrankDynamicsConfiguration configuration) noexcept;

    InertialCrankDynamicsConfiguration configuration_;

    friend std::variant<InertialCrankDynamics, InertialCrankDynamicsError>
    compile_inertial_crank_dynamics(InertialCrankDynamicsConfiguration configuration);
};

using InertialCrankDynamicsCompilation =
    std::variant<InertialCrankDynamics, InertialCrankDynamicsError>;

// Compilation owns a validated copy of the declared inertia and brake curve.
// Brake speeds must be finite, nonnegative, and strictly increasing; torque
// magnitudes must be finite and nonnegative. At least two points are required so
// the complete admitted speed interval has an explicit piecewise-linear segment.
[[nodiscard]] InertialCrankDynamicsCompilation
compile_inertial_crank_dynamics(InertialCrankDynamicsConfiguration configuration);

} // namespace engine_sim_offline::simulation
