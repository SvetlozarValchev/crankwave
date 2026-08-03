#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "presentation/mastering.hpp"
#include "presentation/presentation_method_registry.hpp"
#include "presentation/route_conditioning.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::presentation {

namespace detail {
struct PresentationCalibrationCompiler;
}

class AdmittedPresentationRoute final {
  public:
    [[nodiscard]] contract::RouteId route_id() const noexcept;
    [[nodiscard]] const contract::ResolvedValue<double> &
    source_gain_linear() const noexcept;
    [[nodiscard]] const std::optional<contract::AudioAssetId> &
    impulse_response_asset_id() const noexcept;
    [[nodiscard]] const contract::ResolvedValue<double> &
    impulse_response_gain_linear() const noexcept;
    [[nodiscard]] double wet_mix_01() const noexcept;
    [[nodiscard]] contract::SourceRouteKind source_route_kind() const noexcept;

  private:
    AdmittedPresentationRoute(
        contract::RouteId route_id, contract::ResolvedValue<double> source_gain_linear,
        std::optional<contract::AudioAssetId> impulse_response_asset_id,
        contract::ResolvedValue<double> impulse_response_gain_linear, double wet_mix_01,
        contract::SourceRouteKind source_route_kind);

    contract::RouteId route_id_;
    contract::ResolvedValue<double> source_gain_linear_;
    std::optional<contract::AudioAssetId> impulse_response_asset_id_;
    contract::ResolvedValue<double> impulse_response_gain_linear_;
    double wet_mix_01_ = 0.0;
    contract::SourceRouteKind source_route_kind_ =
        contract::SourceRouteKind::unspecified;

    friend struct detail::PresentationCalibrationCompiler;
};

class AdmittedPresentationCalibration final {
  public:
    static constexpr std::uint64_t source_frames_per_block = 3840;

    AdmittedPresentationCalibration(const AdmittedPresentationCalibration &) = delete;
    AdmittedPresentationCalibration &
    operator=(const AdmittedPresentationCalibration &) = delete;
    AdmittedPresentationCalibration(AdmittedPresentationCalibration &&) noexcept =
        default;
    AdmittedPresentationCalibration &
    operator=(AdmittedPresentationCalibration &&) = delete;

    [[nodiscard]] const PresentationMethodIdentities &methods() const noexcept;
    [[nodiscard]] const RouteConditioningCalibration &conditioning() const noexcept;
    [[nodiscard]] std::span<const AdmittedPresentationRoute> routes() const noexcept;
    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] const contract::ResolvedValue<double> &
    publication_calibration_gain_linear() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId>
    audition_route_ids() const noexcept;
    [[nodiscard]] const MasteringSettings &mastering() const noexcept;
    [[nodiscard]] const contract::RationalRateHz &capture_rate() const noexcept;
    [[nodiscard]] std::uint64_t capture_frames_per_block() const noexcept;
    [[nodiscard]] std::uint64_t total_block_count() const noexcept;
    [[nodiscard]] std::uint64_t pre_audible_block_count() const noexcept;

  private:
    AdmittedPresentationCalibration(
        PresentationMethodIdentities methods, RouteConditioningCalibration conditioning,
        std::vector<AdmittedPresentationRoute> routes,
        contract::ResolvedValue<double> publication_calibration_gain_linear,
        std::vector<contract::RouteId> audition_route_ids, MasteringSettings mastering,
        contract::RationalRateHz capture_rate, std::uint64_t capture_frames_per_block,
        std::uint64_t total_block_count, std::uint64_t pre_audible_block_count);

    PresentationMethodIdentities methods_;
    RouteConditioningCalibration conditioning_;
    std::vector<AdmittedPresentationRoute> routes_;
    contract::ResolvedValue<double> publication_calibration_gain_linear_;
    std::vector<contract::RouteId> audition_route_ids_;
    MasteringSettings mastering_;
    contract::RationalRateHz capture_rate_;
    std::uint64_t capture_frames_per_block_ = 0;
    std::uint64_t total_block_count_ = 0;
    std::uint64_t pre_audible_block_count_ = 0;

    friend struct detail::PresentationCalibrationCompiler;
};

enum class PresentationCalibrationCompileErrorCode : std::uint8_t {
    invalid_resolved_calibration,
};

struct PresentationCalibrationCompileError {
    PresentationCalibrationCompileErrorCode code =
        PresentationCalibrationCompileErrorCode::invalid_resolved_calibration;
    contract::ValidationReport validation;
};

using PresentationCalibrationCompileResult =
    std::variant<AdmittedPresentationCalibration, PresentationCalibrationCompileError>;

[[nodiscard]] PresentationCalibrationCompileResult
compile_presentation_calibration(const contract::PresentationCalibration &calibration,
                                 const contract::EngineSpec &engine,
                                 const contract::RenderScenario &scenario,
                                 const contract::ProvenanceLedger &provenance);

} // namespace engine_sim_offline::presentation
