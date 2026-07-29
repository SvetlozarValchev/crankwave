#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <variant>

namespace engine_sim_offline::experimental {

struct PhysicalListeningWavStats {
    double peak_pressure_pa = 0.0;
    double peak_normalized = 0.0;
    std::uint64_t saturated_sample_count = 0;
    std::uint64_t frame_count = 0;

    friend bool operator==(const PhysicalListeningWavStats &,
                           const PhysicalListeningWavStats &) = default;
};

using PhysicalListeningWavResult = std::variant<PhysicalListeningWavStats, std::string>;

[[nodiscard]] PhysicalListeningWavResult
write_physical_listening_wav(const std::filesystem::path &output_path,
                             std::span<const double> coherent_pressure_pa,
                             std::uint32_t sample_rate_hz,
                             double pascals_per_full_scale, std::string title);

} // namespace engine_sim_offline::experimental
