#pragma once

#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {

inline constexpr std::string_view kResponsiveFixedTransferKind =
    "fixed-overlap-save-complex-spectrum-v1";
inline constexpr std::string_view kResponsivePartitionedTransferKind =
    "uniform-partitioned-overlap-save-complex-spectra-v1";
inline constexpr std::string_view kResponsiveTransferSpectrumEncoding =
    "interleaved-complex-float64le";
inline constexpr std::string_view kResponsivePresentationIdentityMethodId =
    "engine-sim-offline.responsive-presentation-transfer.v1";

enum class ResponsiveTransferShape : std::uint8_t {
    fixed_overlap_save,
    uniform_partitioned_overlap_save,
};

// One immutable configured-IR transfer. Routes with the same exact selected asset
// and gain share one table entry. The bytes are the native presentation kernel's
// canonical little-endian complex spectrum; no time-domain tail is truncated.
struct ResponsiveCompiledTransfer {
    ResponsiveTransferShape shape = ResponsiveTransferShape::fixed_overlap_save;
    std::uint64_t fft_size = 0U;
    std::uint64_t coefficient_count = 0U;
    std::uint64_t partition_frame_count = 0U;
    std::uint64_t partition_count = 0U;
    std::string spectrum_encoding = std::string{kResponsiveTransferSpectrumEncoding};
    std::vector<std::byte> spectrum_bytes;
    contract::Sha256Digest spectrum_sha256;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const ResponsiveCompiledTransfer &,
                           const ResponsiveCompiledTransfer &) = default;
};

struct ResponsivePresentationRoute {
    std::string dry_bus_id;
    std::string source_route_id;
    std::string impulse_response_asset_id;
    contract::Sha256Digest impulse_response_payload_sha256;
    double impulse_response_gain_linear = 0.0;
    double wet_mix_01 = 0.0;
    std::size_t transfer_index = 0U;

    friend bool operator==(const ResponsivePresentationRoute &,
                           const ResponsivePresentationRoute &) = default;
};

struct ResponsiveCompiledPresentation {
    std::string engine_id;
    std::string audition_bus_id;
    std::vector<std::string> audition_dry_bus_order;
    double captured_to_source_scale = 0.0;
    double master_volume_linear = 0.0;
    std::vector<ResponsiveCompiledTransfer> transfers;
    std::vector<ResponsivePresentationRoute> routes;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const ResponsiveCompiledPresentation &,
                           const ResponsiveCompiledPresentation &) = default;
};

using ResponsivePresentationCompileResult =
    std::variant<ResponsiveCompiledPresentation, contract::ValidationReport>;

// Compiles the exact presentation authority already bound into a scenario. The
// scenario is used only as an immutable compiler view and to recover its public dry
// bus roles; no simulation is run here.
[[nodiscard]] ResponsivePresentationCompileResult
compile_responsive_presentation_transfer(
    const compile::CompiledScenario &compiled_scenario);

} // namespace engine_sim_offline::responsive
