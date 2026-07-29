#pragma once

#include <cstddef>
#include <vector>

namespace engine_sim_offline::acoustics {

struct UniformCylindricalWaveguideParameters {
    double length_m = 0.0;
    double inner_area_m2 = 0.0;
    double reference_static_pressure_pa_abs = 0.0;
    double reference_static_temperature_k = 0.0;
    double propagation_loss_nepers_per_m = 0.0;
    double sample_rate_hz = 0.0;
    double universal_gas_constant_j_per_mol_k = 0.0;
    double gas_molar_mass_kg_per_mol = 0.0;
    double heat_capacity_ratio = 0.0;

    friend bool operator==(const UniformCylindricalWaveguideParameters &,
                           const UniformCylindricalWaveguideParameters &) = default;
};

struct UniformCylindricalWaveguideProperties {
    double reference_density_kg_m3 = 0.0;
    double sound_speed_m_s = 0.0;
    double characteristic_impedance_pa_s_m3 = 0.0;
    double delay_frames = 0.0;
    std::size_t integer_delay_frames = 0;
    double fractional_delay_frames = 0.0;
    double one_way_amplitude_survival = 0.0;

    friend bool operator==(const UniformCylindricalWaveguideProperties &,
                           const UniformCylindricalWaveguideProperties &) = default;
};

// Resolves the fixed plane-wave properties used for an entire render. Invalid
// dimensions, thermodynamic references, losses, or a delay below two acoustic
// frames are rejected rather than silently clamped.
[[nodiscard]] UniformCylindricalWaveguideProperties
resolve_uniform_cylindrical_waveguide(
    const UniformCylindricalWaveguideParameters &parameters);

// A causal, zero-state, fixed fractional delay. For D = N + f frames, the
// output is g * ((1 - f) * x[n - N] + f * x[n - N - 1]). The interpolation
// weights are non-negative and sum to one, and 0 < g <= 1, so this primitive
// cannot increase the peak or frequency-domain magnitude of a finite input.
class FixedPassiveFractionalDelay final {
  public:
    FixedPassiveFractionalDelay(double delay_frames, double amplitude_survival);

    // Reads the arrival for the current frame without advancing history. This is
    // the first half of a network update: every boundary may inspect all current
    // arrivals before any launch for that frame is committed.
    [[nodiscard]] double arrival() const;
    void commit(double input);

    // Exact convenience wrapper for an isolated line: arrival(), then commit().
    [[nodiscard]] double process(double input);

    [[nodiscard]] double delay_frames() const noexcept;
    [[nodiscard]] std::size_t integer_delay_frames() const noexcept;
    [[nodiscard]] double fractional_delay_frames() const noexcept;
    [[nodiscard]] double amplitude_survival() const noexcept;

  private:
    double delay_frames_ = 0.0;
    std::size_t integer_delay_frames_ = 0;
    double fractional_delay_frames_ = 0.0;
    double amplitude_survival_ = 0.0;
    std::vector<double> history_;
    std::size_t write_index_ = 0;
};

struct WaveguideLaunchFrame {
    // Pressure wave launched at the upstream end toward the downstream end.
    double from_upstream_pa = 0.0;
    // Pressure wave launched at the downstream end toward the upstream end.
    double from_downstream_pa = 0.0;

    friend bool operator==(const WaveguideLaunchFrame &,
                           const WaveguideLaunchFrame &) = default;
};

struct WaveguideArrivalFrame {
    // Delayed wave arriving at the upstream end from downstream.
    double at_upstream_pa = 0.0;
    // Delayed wave arriving at the downstream end from upstream.
    double at_downstream_pa = 0.0;

    friend bool operator==(const WaveguideArrivalFrame &,
                           const WaveguideArrivalFrame &) = default;
};

// Two independent traveling-wave delay lines sharing one resolved cylindrical
// duct. The two directions never share mutable history.
class UniformCylindricalWaveguide final {
  public:
    explicit UniformCylindricalWaveguide(
        UniformCylindricalWaveguideParameters parameters);

    // Network stepping is deliberately two-phase. Read all duct arrivals for one
    // acoustic frame, solve every connected boundary, then commit every launch.
    [[nodiscard]] WaveguideArrivalFrame arrivals() const;
    void commit(const WaveguideLaunchFrame &launched);

    // Exact convenience wrapper for an isolated line: arrivals(), then commit().
    [[nodiscard]] WaveguideArrivalFrame process(const WaveguideLaunchFrame &launched);

    [[nodiscard]] const UniformCylindricalWaveguideParameters &
    parameters() const noexcept;
    [[nodiscard]] const UniformCylindricalWaveguideProperties &
    properties() const noexcept;

  private:
    UniformCylindricalWaveguideParameters parameters_;
    UniformCylindricalWaveguideProperties properties_;
    FixedPassiveFractionalDelay upstream_to_downstream_;
    FixedPassiveFractionalDelay downstream_to_upstream_;
};

} // namespace engine_sim_offline::acoustics
