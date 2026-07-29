#pragma once

#include <array>
#include <cstddef>

namespace engine_sim_offline::acoustics {

inline constexpr std::size_t kCompactFourPortCount = 4;
using CompactFourPortWaves = std::array<double, kCompactFourPortCount>;

struct CompactFourPortScattering {
    double common_pressure_pa = 0.0;
    CompactFourPortWaves departing_pressure_waves_pa{};

    friend bool operator==(const CompactFourPortScattering &,
                           const CompactFourPortScattering &) = default;
};

// An ideal, lossless compact junction. Each arriving wave travels toward the
// junction and each departing wave travels away from it. Port volume flow is
// positive toward the junction: U_i = (a_i - b_i) / Zc_i.
class IdealCompactFourPortJunction final {
  public:
    explicit IdealCompactFourPortJunction(
        CompactFourPortWaves characteristic_impedances_pa_s_m3);

    [[nodiscard]] CompactFourPortScattering
    scatter(const CompactFourPortWaves &arriving_pressure_waves_pa) const;

    [[nodiscard]] const CompactFourPortWaves &
    characteristic_impedances_pa_s_m3() const noexcept;
    [[nodiscard]] const CompactFourPortWaves &
    characteristic_admittances_m3_pa_s() const noexcept;

  private:
    CompactFourPortWaves characteristic_impedances_pa_s_m3_{};
    CompactFourPortWaves characteristic_admittances_m3_pa_s_{};
    double admittance_sum_m3_pa_s_ = 0.0;
};

} // namespace engine_sim_offline::acoustics
