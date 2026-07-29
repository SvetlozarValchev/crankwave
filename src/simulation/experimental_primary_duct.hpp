#pragma once

#include <array>
#include <cstddef>

namespace engine_sim_offline::simulation {

// Disposable listening spike: a one-way nonlinear acoustic projection driven by
// the existing chamber and valve state. It deliberately does not feed pressure
// back into the canonical gas/power simulation.
class ExperimentalPrimaryDuct final {
  public:
    struct Observation {
        double static_pressure_pa_abs = 0.0;
        double dynamic_pressure_forward_pa = 0.0;
        double dynamic_pressure_reverse_pa = 0.0;

        friend bool operator==(const Observation &, const Observation &) = default;
    };

    struct Conserved {
        double density_kg_m3 = 0.0;
        double momentum_kg_m2_s = 0.0;
        double total_energy_j_m3 = 0.0;
    };

    void initialize(double ambient_pressure_pa, double initial_temperature_k,
                    double length_m, double cross_section_area_m2,
                    double gas_step_s) noexcept;

    [[nodiscard]] bool advance(double chamber_pressure_pa_abs,
                               double chamber_temperature_k,
                               double exhaust_valve_restriction_k) noexcept;

    [[nodiscard]] Observation observation() const noexcept;

  private:
    static constexpr std::size_t kCellCount = 20U;
    static constexpr std::size_t kInternalSubstepCount = 2U;

    struct ReflectionFilter {
        double b0 = 0.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double x1 = 0.0;
        double x2 = 0.0;
        double y1 = 0.0;
        double y2 = 0.0;

        [[nodiscard]] double process(double input) noexcept;
    };

    std::array<Conserved, kCellCount> cells_{};
    std::array<Conserved, kCellCount> next_{};
    ReflectionFilter outlet_reflection_{};
    double ambient_pressure_pa_ = 0.0;
    double ambient_temperature_k_ = 0.0;
    double ambient_density_kg_m3_ = 0.0;
    double ambient_sound_speed_m_s_ = 0.0;
    double characteristic_impedance_pa_s_m_ = 0.0;
    double wall_temperature_k_ = 0.0;
    double cross_section_area_m2_ = 0.0;
    double diameter_m_ = 0.0;
    double cell_length_m_ = 0.0;
    double internal_step_s_ = 0.0;
    bool initialized_ = false;
};

} // namespace engine_sim_offline::simulation
