#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace engine_sim_offline::dsp {

// Immutable-after-construction coefficient table for the causal
// reconstruction method. Row 4096 is the explicitly shifted phase-wrap row.
class CausalReconstructionTable {
  public:
    static constexpr std::size_t tap_count = 257;
    static constexpr std::size_t half_width = 128;
    static constexpr std::size_t phase_interval_count = 4096;
    static constexpr std::size_t row_count = phase_interval_count + 1;
    static constexpr std::size_t coefficient_count = row_count * tap_count;

    CausalReconstructionTable();

    CausalReconstructionTable(const CausalReconstructionTable &) = delete;
    CausalReconstructionTable &operator=(const CausalReconstructionTable &) = delete;
    CausalReconstructionTable(CausalReconstructionTable &&) noexcept = default;
    CausalReconstructionTable &
    operator=(CausalReconstructionTable &&) noexcept = default;

    [[nodiscard]] std::span<const double, tap_count> phase_row(std::size_t phase) const;
    [[nodiscard]] double coefficient(std::size_t phase, std::size_t tap) const;

  private:
    std::vector<double> coefficients_;
};

} // namespace engine_sim_offline::dsp
