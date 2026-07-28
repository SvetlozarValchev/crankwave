#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "presentation/mastering.hpp"
#include "presentation/presentation_method_registry.hpp"
#include "presentation/route_conditioning.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace engine_sim_offline::presentation {

namespace detail {
struct PresentationCalibrationCompiler;
}

class AdmittedPresentationRoute final {
  public:
    [[nodiscard]] contract::RouteId route_id() const noexcept;
    [[nodiscard]] contract::AudioAssetId impulse_response_asset_id() const noexcept;
    [[nodiscard]] const contract::ResolvedValue<double> &
    impulse_response_gain_linear() const noexcept;
    [[nodiscard]] double wet_mix_01() const noexcept;

  private:
    AdmittedPresentationRoute(
        contract::RouteId route_id, contract::AudioAssetId impulse_response_asset_id,
        contract::ResolvedValue<double> impulse_response_gain_linear,
        double wet_mix_01);

    contract::RouteId route_id_;
    contract::AudioAssetId impulse_response_asset_id_;
    contract::ResolvedValue<double> impulse_response_gain_linear_;
    double wet_mix_01_ = 0.0;

    friend struct detail::PresentationCalibrationCompiler;
};

class AdmittedPresentationCalibration final {
  public:
    static constexpr std::size_t route_count = 2;
    static constexpr std::uint64_t capture_frames_per_block = 200;
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
    [[nodiscard]] const std::array<AdmittedPresentationRoute, route_count> &
    routes() const noexcept;
    [[nodiscard]] const contract::ResolvedValue<double> &
    publication_calibration_gain_linear() const noexcept;
    [[nodiscard]] const std::array<contract::RouteId, route_count> &
    audition_route_ids() const noexcept;
    [[nodiscard]] const MasteringSettings &mastering() const noexcept;
    [[nodiscard]] std::uint64_t total_block_count() const noexcept;
    [[nodiscard]] std::uint64_t pre_audible_block_count() const noexcept;

  private:
    AdmittedPresentationCalibration(
        PresentationMethodIdentities methods, RouteConditioningCalibration conditioning,
        std::array<AdmittedPresentationRoute, route_count> routes,
        contract::ResolvedValue<double> publication_calibration_gain_linear,
        std::array<contract::RouteId, route_count> audition_route_ids,
        MasteringSettings mastering, std::uint64_t total_block_count,
        std::uint64_t pre_audible_block_count);

    PresentationMethodIdentities methods_;
    RouteConditioningCalibration conditioning_;
    std::array<AdmittedPresentationRoute, route_count> routes_;
    contract::ResolvedValue<double> publication_calibration_gain_linear_;
    std::array<contract::RouteId, route_count> audition_route_ids_;
    MasteringSettings mastering_;
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
