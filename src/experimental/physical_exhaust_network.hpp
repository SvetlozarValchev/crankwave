#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace engine_sim_offline::experimental {

inline constexpr std::uint32_t kPhysicalExhaustSampleRateHz = 192000U;

struct PhysicalValveBoundary {
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double molar_flow_conductance = 0.0;
};

struct PhysicalExhaustOutput {
    std::array<double, 2> radiated_pressure_pa{};
    double coherent_mono_pressure_pa = 0.0;
    double maximum_cfl = 0.0;
};

class PhysicalExhaustNetwork final {
  public:
    void initialize(double ambient_pressure_pa, double ambient_temperature_k);

    [[nodiscard]] bool advance(const std::array<PhysicalValveBoundary, 6> &valves);

    [[nodiscard]] const PhysicalExhaustOutput &output() const;
    [[nodiscard]] std::string_view failure_reason() const;

  private:
    static constexpr std::size_t kCylinderCount = 6U;
    static constexpr std::size_t kBankCount = 2U;
    static constexpr std::size_t kCylindersPerBank = 3U;
    static constexpr std::size_t kPrimaryCellCount = 12U;
    static constexpr std::size_t kTailpipeCellCount = 60U;

    struct Conserved {
        double density_kg_m3 = 0.0;
        double momentum_kg_m2_s = 0.0;
        double total_energy_j_m3 = 0.0;
    };

    struct Primitive {
        double density_kg_m3 = 0.0;
        double velocity_m_s = 0.0;
        double pressure_pa = 0.0;
        double temperature_k = 0.0;
        double sound_speed_m_s = 0.0;
    };

    struct Flux {
        double mass_kg_m2_s = 0.0;
        double momentum_pa = 0.0;
        double energy_w_m2 = 0.0;
    };

    struct MixedJunction {
        double mass_kg = 0.0;
        double total_energy_j = 0.0;
    };

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

    [[nodiscard]] static bool finite_positive(double value) noexcept;
    [[nodiscard]] static bool primitive(const Conserved &state,
                                        Primitive &value) noexcept;
    [[nodiscard]] static Conserved conserved(double density_kg_m3, double velocity_m_s,
                                             double pressure_pa) noexcept;
    [[nodiscard]] static Flux physical_flux(const Conserved &state,
                                            const Primitive &value) noexcept;
    [[nodiscard]] static Flux hll_flux(const Conserved &left, const Conserved &right,
                                       const Primitive &left_value,
                                       const Primitive &right_value) noexcept;

    [[nodiscard]] bool valve_flux(const PhysicalValveBoundary &valve,
                                  const Primitive &primary_inlet,
                                  Flux &result) noexcept;
    [[nodiscard]] bool outlet_flux(std::size_t bank, const Conserved &tailpipe_outlet,
                                   const Primitive &tailpipe_outlet_value, Flux &result,
                                   double &volume_flow_m3_s) noexcept;
    [[nodiscard]] bool apply_losses(Conserved &state, double diameter_m,
                                    double wall_temperature_k) noexcept;
    [[nodiscard]] bool fail(std::string_view reason) noexcept;

    std::array<std::array<Conserved, kPrimaryCellCount>, kCylinderCount> primaries_{};
    std::array<std::array<Conserved, kPrimaryCellCount>, kCylinderCount>
        next_primaries_{};
    std::array<std::array<Conserved, kTailpipeCellCount>, kBankCount> tailpipes_{};
    std::array<std::array<Conserved, kTailpipeCellCount>, kBankCount> next_tailpipes_{};
    std::array<MixedJunction, kBankCount> junctions_{};
    std::array<MixedJunction, kBankCount> next_junctions_{};
    std::array<ReflectionFilter, kBankCount> outlet_reflections_{};
    std::array<double, kBankCount> previous_outlet_volume_flow_m3_s_{};

    PhysicalExhaustOutput output_{};
    std::string_view failure_reason_{};
    double ambient_pressure_pa_ = 0.0;
    double ambient_temperature_k_ = 0.0;
    double ambient_density_kg_m3_ = 0.0;
    double tailpipe_reference_density_kg_m3_ = 0.0;
    double tailpipe_reference_sound_speed_m_s_ = 0.0;
    double tailpipe_characteristic_impedance_pa_s_m_ = 0.0;
    bool initialized_ = false;
};

} // namespace engine_sim_offline::experimental
