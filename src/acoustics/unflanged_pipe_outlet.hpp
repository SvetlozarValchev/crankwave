#pragma once

#include <cstddef>
#include <vector>

namespace engine_sim_offline::acoustics {

struct UnflangedPipeOutletParameters {
    double outlet_radius_m = 0.0;
    double pipe_sound_speed_m_s = 0.0;
    double characteristic_impedance_pa_s_m3 = 0.0;
    double sample_rate_hz = 0.0;
    double ambient_density_kg_m3 = 0.0;
    double ambient_sound_speed_m_s = 0.0;
    double observation_distance_m = 0.0;

    friend bool operator==(const UnflangedPipeOutletParameters &,
                           const UnflangedPipeOutletParameters &) = default;
};

struct DigitalReflectionBiquad {
    // H(z) = (b0 + b1 z^-1 + b2 z^-2) /
    //        (1  + a1 z^-1 + a2 z^-2).
    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    friend bool operator==(const DigitalReflectionBiquad &,
                           const DigitalReflectionBiquad &) = default;
};

struct UnflangedPipeOutletProperties {
    double radius_time_s = 0.0;
    DigitalReflectionBiquad reflection;
    double first_pole_magnitude = 0.0;
    double second_pole_magnitude = 0.0;
    double maximum_sampled_reflection_magnitude = 0.0;
    std::size_t passivity_frequency_sample_count = 0;
    double observer_delay_frames = 0.0;
    double monopole_far_field_scale_kg_per_m4 = 0.0;

    friend bool operator==(const UnflangedPipeOutletProperties &,
                           const UnflangedPipeOutletProperties &) = default;
};

// Resolves the frozen Silva et al. unflanged-pipe reflection approximation with
// s = 2 fs (1 - z^-1) / (1 + z^-1). Construction rejects unstable poles or a
// sampled digital reflection magnitude above unity beyond numerical tolerance.
[[nodiscard]] UnflangedPipeOutletProperties
resolve_unflanged_pipe_outlet(const UnflangedPipeOutletParameters &parameters);

struct UnflangedPipeOutletFrame {
    // Pressure wave returned from the outlet toward the pipe interior.
    double reflected_pressure_wave_pa = 0.0;
    // Positive volume velocity flows out of the pipe into the exterior field.
    double outlet_volume_velocity_m3_s = 0.0;
    // Compact-monopole free-field pressure at the declared observation point.
    double radiated_pressure_pa = 0.0;

    friend bool operator==(const UnflangedPipeOutletFrame &,
                           const UnflangedPipeOutletFrame &) = default;
};

// One fixed, causal outlet boundary. The incident pressure wave is filtered by
// the unflanged termination; its resulting outlet volume velocity is projected
// through one backward difference and the physical observer propagation delay.
// State is continuous across any caller-owned block partitioning.
class UnflangedPipeOutlet final {
  public:
    explicit UnflangedPipeOutlet(UnflangedPipeOutletParameters parameters);

    [[nodiscard]] UnflangedPipeOutletFrame process(double incident_pressure_wave_pa);

    [[nodiscard]] const UnflangedPipeOutletParameters &parameters() const noexcept;
    [[nodiscard]] const UnflangedPipeOutletProperties &properties() const noexcept;

  private:
    [[nodiscard]] double preview_observer_delay(double input) const;
    void commit_observer_delay(double input) noexcept;

    UnflangedPipeOutletParameters parameters_;
    UnflangedPipeOutletProperties properties_;

    double incident_z1_pa_ = 0.0;
    double incident_z2_pa_ = 0.0;
    double reflected_z1_pa_ = 0.0;
    double reflected_z2_pa_ = 0.0;
    double previous_outlet_volume_velocity_m3_s_ = 0.0;

    std::size_t observer_integer_delay_frames_ = 0;
    double observer_fractional_delay_frames_ = 0.0;
    std::vector<double> observer_history_;
    std::size_t observer_write_index_ = 0;
};

} // namespace engine_sim_offline::acoustics
