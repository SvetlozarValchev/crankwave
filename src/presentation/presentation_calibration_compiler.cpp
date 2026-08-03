#include "presentation/presentation_calibration_compiler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace engine_sim_offline::presentation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

constexpr contract::RationalRateHz kCaptureRate{20000, 1};
constexpr std::uint64_t kCaptureFramesPerBlock = 400;
constexpr contract::RationalRateHz kSourceRate{192000, 1};
constexpr std::uint64_t kPresentationBlocksPerSecond = 50;
constexpr std::uint64_t kFloat32WaveBytesPerFrame = 4;
constexpr std::uint64_t kFloat32WaveRiffFixedByteCount = 50;

static_assert(AdmittedPresentationCalibration::source_frames_per_block *
                  kPresentationBlocksPerSecond ==
              kSourceRate.numerator);

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool canonical_nonnegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 &&
           (value != 0.0 || !std::signbit(value));
}

[[nodiscard]] bool canonical_unit_interval(double value) noexcept {
    return canonical_nonnegative(value) && value <= 1.0;
}

[[nodiscard]] bool checked_add(std::uint64_t left, std::uint64_t right,
                               std::uint64_t &result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t &result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

void require_exact_rate(ValidationReport &report,
                        const contract::RationalRateHz &actual,
                        const contract::RationalRateHz &expected, std::string path) {
    require(report, actual == expected, ContractIssueCode::unsupported_value,
            std::move(path),
            "rate does not match the exact executable presentation clock");
}

[[nodiscard]] std::uint64_t
admitted_capture_frames_per_block(const contract::RationalRateHz &rate) noexcept {
    return rate == kCaptureRate ? kCaptureFramesPerBlock : 0;
}

void require_canonical_zero(ValidationReport &report, double value, std::string path) {
    require(report, value != 0.0 || !std::signbit(value),
            ContractIssueCode::invalid_value, std::move(path),
            "zero must use the canonical positive binary64 representation");
}

[[nodiscard]] bool
route_uses_asset(const contract::PresentationCalibration &calibration,
                 contract::AudioAssetId asset_id) noexcept {
    return std::ranges::any_of(calibration.routes, [asset_id](const auto &route) {
        return route.impulse_response_asset_id ==
               std::optional<contract::AudioAssetId>{asset_id};
    });
}

[[nodiscard]] const contract::RoutePresentation *
find_configured_route(const contract::PresentationCalibration &calibration,
                      contract::RouteId route_id) noexcept {
    const auto found = std::ranges::find(calibration.routes, route_id,
                                         &contract::RoutePresentation::route_id);
    return found == calibration.routes.end() ? nullptr : &*found;
}

} // namespace

AdmittedPresentationRoute::AdmittedPresentationRoute(
    contract::RouteId route_id, contract::ResolvedValue<double> source_gain_linear,
    std::optional<contract::AudioAssetId> impulse_response_asset_id,
    contract::ResolvedValue<double> impulse_response_gain_linear, double wet_mix_01,
    contract::SourceRouteKind source_route_kind)
    : route_id_(route_id), source_gain_linear_(std::move(source_gain_linear)),
      impulse_response_asset_id_(impulse_response_asset_id),
      impulse_response_gain_linear_(std::move(impulse_response_gain_linear)),
      wet_mix_01_(wet_mix_01), source_route_kind_(source_route_kind) {}

contract::RouteId AdmittedPresentationRoute::route_id() const noexcept {
    return route_id_;
}

const contract::ResolvedValue<double> &
AdmittedPresentationRoute::source_gain_linear() const noexcept {
    return source_gain_linear_;
}

const std::optional<contract::AudioAssetId> &
AdmittedPresentationRoute::impulse_response_asset_id() const noexcept {
    return impulse_response_asset_id_;
}

const contract::ResolvedValue<double> &
AdmittedPresentationRoute::impulse_response_gain_linear() const noexcept {
    return impulse_response_gain_linear_;
}

double AdmittedPresentationRoute::wet_mix_01() const noexcept {
    return wet_mix_01_;
}

contract::SourceRouteKind
AdmittedPresentationRoute::source_route_kind() const noexcept {
    return source_route_kind_;
}

AdmittedPresentationCalibration::AdmittedPresentationCalibration(
    PresentationMethodIdentities methods, RouteConditioningCalibration conditioning,
    std::vector<AdmittedPresentationRoute> routes,
    contract::ResolvedValue<double> publication_calibration_gain_linear,
    std::vector<contract::RouteId> audition_route_ids, MasteringSettings mastering,
    contract::RationalRateHz capture_rate, std::uint64_t capture_frames_per_block,
    std::uint64_t total_block_count, std::uint64_t pre_audible_block_count)
    : methods_(std::move(methods)), conditioning_(conditioning),
      routes_(std::move(routes)), publication_calibration_gain_linear_(
                                      std::move(publication_calibration_gain_linear)),
      audition_route_ids_(audition_route_ids), mastering_(std::move(mastering)),
      capture_rate_(capture_rate), capture_frames_per_block_(capture_frames_per_block),
      total_block_count_(total_block_count),
      pre_audible_block_count_(pre_audible_block_count) {}

const PresentationMethodIdentities &
AdmittedPresentationCalibration::methods() const noexcept {
    return methods_;
}

const RouteConditioningCalibration &
AdmittedPresentationCalibration::conditioning() const noexcept {
    return conditioning_;
}

std::span<const AdmittedPresentationRoute>
AdmittedPresentationCalibration::routes() const noexcept {
    return routes_;
}

std::size_t AdmittedPresentationCalibration::route_count() const noexcept {
    return routes_.size();
}

const contract::ResolvedValue<double> &
AdmittedPresentationCalibration::publication_calibration_gain_linear() const noexcept {
    return publication_calibration_gain_linear_;
}

std::span<const contract::RouteId>
AdmittedPresentationCalibration::audition_route_ids() const noexcept {
    return audition_route_ids_;
}

const MasteringSettings &AdmittedPresentationCalibration::mastering() const noexcept {
    return mastering_;
}

const contract::RationalRateHz &
AdmittedPresentationCalibration::capture_rate() const noexcept {
    return capture_rate_;
}

std::uint64_t
AdmittedPresentationCalibration::capture_frames_per_block() const noexcept {
    return capture_frames_per_block_;
}

std::uint64_t AdmittedPresentationCalibration::total_block_count() const noexcept {
    return total_block_count_;
}

std::uint64_t
AdmittedPresentationCalibration::pre_audible_block_count() const noexcept {
    return pre_audible_block_count_;
}

namespace detail {

struct PresentationCalibrationCompiler {
    [[nodiscard]] static PresentationCalibrationCompileResult
    compile(const contract::PresentationCalibration &calibration,
            const contract::EngineSpec &engine,
            const contract::RenderScenario &scenario,
            const contract::ProvenanceLedger &provenance) {
        ValidationReport report =
            contract::validate(calibration, engine, scenario, provenance);
        report.append(admit_implemented_presentation_methods(calibration.methods));

        const auto capture_frames_per_block =
            admitted_capture_frames_per_block(scenario.rates.capture);
        require(report, scenario.rates.physics == scenario.rates.capture,
                ContractIssueCode::unsupported_value, "scenario.rates.physics",
                "physics and capture must use the same executable clock");
        require(report, capture_frames_per_block != 0,
                ContractIssueCode::unsupported_value, "scenario.rates.capture",
                "capture must use the exact 20000/1 executable clock");
        require_exact_rate(report, scenario.rates.source_processing, kSourceRate,
                           "scenario.rates.source_processing");
        require_exact_rate(report, scenario.rates.acoustic, kSourceRate,
                           "scenario.rates.acoustic");
        require_exact_rate(report, scenario.rates.delivery, kSourceRate,
                           "scenario.rates.delivery");
        require(report,
                scenario.quality.value.capture_block_capacity_frames >=
                    capture_frames_per_block,
                ContractIssueCode::unsupported_value,
                "scenario.quality.value.capture_block_capacity_frames",
                "capture transport cannot hold one exact 20 ms presentation block");

        const auto total_capture = contract::resolve_frame_index(
            scenario.total_duration_s.value, scenario.rates.capture);
        const auto pre_audible_capture = contract::resolve_frame_index(
            scenario.audible_start_s.value, scenario.rates.capture);
        const auto audible_capture = contract::resolve_frame_index(
            scenario.audible_duration_s.value, scenario.rates.capture);
        const auto total_source =
            contract::resolve_frame_index(scenario.total_duration_s.value, kSourceRate);
        const auto pre_audible_source =
            contract::resolve_frame_index(scenario.audible_start_s.value, kSourceRate);
        const auto audible_source = contract::resolve_frame_index(
            scenario.audible_duration_s.value, kSourceRate);

        require(report, total_capture.has_value() && *total_capture > 0,
                ContractIssueCode::inconsistent_semantics,
                "scenario.total_duration_s.value",
                "total duration must resolve to a positive capture-frame horizon");
        require(report, pre_audible_capture.has_value(),
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_start_s.value",
                "audible start must resolve to an exact capture-frame boundary");
        require(report, audible_capture.has_value() && *audible_capture > 0,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_duration_s.value",
                "audible duration must resolve to a positive capture-frame horizon");
        require(report, total_source.has_value(),
                ContractIssueCode::inconsistent_semantics,
                "scenario.total_duration_s.value",
                "total duration must resolve to an exact source-frame horizon");
        require(report, pre_audible_source.has_value(),
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_start_s.value",
                "audible start must resolve to an exact source-frame boundary");
        require(report, audible_source.has_value() && *audible_source > 0,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_duration_s.value",
                "audible duration must resolve to a positive source-frame horizon");

        std::uint64_t total_blocks = 0;
        std::uint64_t pre_audible_blocks = 0;
        if (total_capture.has_value()) {
            require(report,
                    capture_frames_per_block != 0 &&
                        *total_capture % capture_frames_per_block == 0,
                    ContractIssueCode::inconsistent_semantics,
                    "scenario.total_duration_s.value",
                    "total duration must align to a complete 20 ms presentation "
                    "block");
            if (capture_frames_per_block != 0) {
                total_blocks = *total_capture / capture_frames_per_block;
            }
        }
        if (pre_audible_capture.has_value()) {
            require(report,
                    capture_frames_per_block != 0 &&
                        *pre_audible_capture % capture_frames_per_block == 0,
                    ContractIssueCode::inconsistent_semantics,
                    "scenario.audible_start_s.value",
                    "audible start must align to a complete 20 ms presentation "
                    "block");
            if (capture_frames_per_block != 0) {
                pre_audible_blocks = *pre_audible_capture / capture_frames_per_block;
            }
        }

        std::uint64_t capture_end = 0;
        const bool capture_end_representable =
            pre_audible_capture.has_value() && audible_capture.has_value() &&
            checked_add(*pre_audible_capture, *audible_capture, capture_end);
        require(report,
                capture_end_representable && total_capture.has_value() &&
                    capture_end == *total_capture,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_duration_s.value",
                "audible end must equal the total presentation horizon");

        std::uint64_t source_end = 0;
        const bool source_end_representable =
            pre_audible_source.has_value() && audible_source.has_value() &&
            checked_add(*pre_audible_source, *audible_source, source_end);
        require(report,
                source_end_representable && total_source.has_value() &&
                    source_end == *total_source,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_duration_s.value",
                "audible source end must equal the total presentation horizon");

        require(report, total_blocks > pre_audible_blocks,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_duration_s.value",
                "presentation requires a positive audible block interval");

        std::uint64_t represented_total_capture = 0;
        std::uint64_t represented_total_source = 0;
        std::uint64_t represented_pre_source = 0;
        std::uint64_t audible_source_frames = 0;
        const bool total_capture_product_ok = checked_multiply(
            total_blocks, capture_frames_per_block, represented_total_capture);
        const bool total_source_product_ok = checked_multiply(
            total_blocks, AdmittedPresentationCalibration::source_frames_per_block,
            represented_total_source);
        const bool pre_source_product_ok =
            checked_multiply(pre_audible_blocks,
                             AdmittedPresentationCalibration::source_frames_per_block,
                             represented_pre_source);
        const bool audible_source_product_ok =
            total_blocks >= pre_audible_blocks &&
            checked_multiply(total_blocks - pre_audible_blocks,
                             AdmittedPresentationCalibration::source_frames_per_block,
                             audible_source_frames);
        require(report,
                total_capture_product_ok && total_capture.has_value() &&
                    represented_total_capture == *total_capture,
                ContractIssueCode::inconsistent_semantics,
                "scenario.total_duration_s.value",
                "total capture frame product is not exactly representable");
        require(report,
                total_source_product_ok && total_source.has_value() &&
                    represented_total_source == *total_source,
                ContractIssueCode::inconsistent_semantics,
                "scenario.total_duration_s.value",
                "total source frame product is not exactly representable");
        require(report,
                pre_source_product_ok && pre_audible_source.has_value() &&
                    represented_pre_source == *pre_audible_source,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_start_s.value",
                "pre-audible source frame product is not exactly representable");
        require(report,
                audible_source_product_ok && audible_source.has_value() &&
                    audible_source_frames == *audible_source,
                ContractIssueCode::inconsistent_semantics,
                "scenario.audible_duration_s.value",
                "audible source frame product is not exactly representable");
        std::uint64_t float32_wave_data_bytes = 0;
        const bool float32_wave_representable =
            checked_multiply(audible_source_frames, kFloat32WaveBytesPerFrame,
                             float32_wave_data_bytes) &&
            float32_wave_data_bytes <= std::numeric_limits<std::uint32_t>::max() -
                                           kFloat32WaveRiffFixedByteCount;
        require(report, float32_wave_representable,
                ContractIssueCode::unsupported_value,
                "scenario.audible_duration_s.value",
                "audible horizon exceeds the exact Float32 WAVE publication limit");

        require(report, !engine.routes.empty(), ContractIssueCode::missing_value,
                "engine.routes",
                "the executable presentation requires at least one engine source "
                "route");
        for (std::size_t index = 0; index < engine.routes.size(); ++index) {
            const auto kind = engine.routes[index].kind.value;
            require(report,
                    kind == contract::SourceRouteKind::exhaust_outlet ||
                        kind == contract::SourceRouteKind::intake_inlet,
                    ContractIssueCode::unsupported_value,
                    "engine.routes[" + std::to_string(index) + "].kind.value",
                    "the executable presentation accepts gas source routes only");
        }

        require(report, calibration.routes.size() == engine.routes.size(),
                ContractIssueCode::unsupported_value, "presentation.routes",
                "the executable presentation requires one configured route for "
                "every engine source route");
        for (std::size_t index = 0; index < calibration.routes.size(); ++index) {
            const auto &route = calibration.routes[index];
            const auto path = "presentation.routes[" + std::to_string(index) + "]";
            const auto engine_route = std::ranges::find(engine.routes, route.route_id,
                                                        &contract::RouteSpec::id);
            const bool exhaust =
                engine_route != engine.routes.end() &&
                engine_route->kind.value == contract::SourceRouteKind::exhaust_outlet;
            const bool intake =
                engine_route != engine.routes.end() &&
                engine_route->kind.value == contract::SourceRouteKind::intake_inlet;
            require(report, canonical_nonnegative(route.source_gain_linear.value),
                    ContractIssueCode::invalid_value,
                    path + ".source_gain_linear.value",
                    "source gain must be finite, nonnegative, and use canonical "
                    "positive zero");
            require(report,
                    (exhaust && route.impulse_response_asset_id.has_value()) ||
                        (intake && !route.impulse_response_asset_id.has_value()),
                    ContractIssueCode::inconsistent_semantics,
                    path + ".impulse_response_asset_id",
                    "active exhaust routes require a transfer asset while "
                    "declared-silent intake routes require none");
            require(report,
                    canonical_nonnegative(route.impulse_response_gain_linear.value),
                    ContractIssueCode::invalid_value,
                    path + ".impulse_response_gain_linear.value",
                    "impulse-response gain must be finite, nonnegative, and use "
                    "canonical positive zero");
            require(report, canonical_unit_interval(route.wet_mix_01.value),
                    ContractIssueCode::invalid_value, path + ".wet_mix_01.value",
                    "wet mix must be finite in [0, 1] and use canonical positive "
                    "zero");
            if (intake) {
                require(report,
                        route.impulse_response_gain_linear.value == 0.0 &&
                            !std::signbit(route.impulse_response_gain_linear.value) &&
                            route.wet_mix_01.value == 0.0 &&
                            !std::signbit(route.wet_mix_01.value),
                        ContractIssueCode::unsupported_value, path,
                        "declared-silent intake transfer values must be canonical "
                        "positive zero");
            }
        }
        if (calibration.routes.size() == engine.routes.size()) {
            for (std::size_t index = 0; index < engine.routes.size(); ++index) {
                require(report,
                        find_configured_route(calibration, engine.routes[index].id) !=
                            nullptr,
                        ContractIssueCode::dangling_reference,
                        "engine.routes[" + std::to_string(index) + "].id",
                        "engine source route is missing its presentation calibration");
            }
        }

        for (std::size_t index = 0; index < calibration.assets.size(); ++index) {
            require(report, route_uses_asset(calibration, calibration.assets[index].id),
                    ContractIssueCode::unsupported_value,
                    "presentation.assets[" + std::to_string(index) + "]",
                    "presentation asset is not used by any configured route");
        }

        require(report,
                calibration.audition.selected_routes.value.size() ==
                    engine.routes.size(),
                ContractIssueCode::unsupported_value,
                "presentation.audition.selected_routes.value",
                "the executable audition selection must declare every configured "
                "route once; its active-exhaust subsequence owns deterministic "
                "arithmetic order");

        const RouteConditioningCalibration conditioning{
            calibration.conditioning.jitter_scale.value,
            calibration.conditioning.jitter_modulation_cutoff_hz.value,
            calibration.conditioning.derivative_mix_01.value,
            calibration.conditioning.air_noise_mix_01.value,
            calibration.conditioning.air_noise_cutoff_hz.value,
        };
        require(report, valid_route_conditioning_calibration(conditioning),
                ContractIssueCode::unsupported_value, "presentation.conditioning",
                "conditioning values are outside the exact executable method "
                "domain");
        require_canonical_zero(report, calibration.conditioning.jitter_scale.value,
                               "presentation.conditioning.jitter_scale.value");
        require_canonical_zero(
            report, calibration.conditioning.jitter_modulation_cutoff_hz.value,
            "presentation.conditioning.jitter_modulation_cutoff_hz.value");
        require_canonical_zero(report, calibration.conditioning.derivative_mix_01.value,
                               "presentation.conditioning.derivative_mix_01.value");
        require_canonical_zero(report, calibration.conditioning.air_noise_mix_01.value,
                               "presentation.conditioning.air_noise_mix_01.value");
        require_canonical_zero(report,
                               calibration.conditioning.air_noise_cutoff_hz.value,
                               "presentation.conditioning.air_noise_cutoff_hz.value");

        require(report,
                std::isfinite(calibration.publication.calibration_gain_linear.value) &&
                    calibration.publication.calibration_gain_linear.value > 0.0,
                ContractIssueCode::invalid_value,
                "presentation.publication.calibration_gain_linear.value",
                "publication calibration gain must be finite and strictly positive");

        const double monitoring_gain =
            calibration.audition.monitoring_gain_linear.value;
        const bool monitoring_gain_in_float32_range =
            std::isfinite(monitoring_gain) && monitoring_gain > 0.0 &&
            monitoring_gain <= static_cast<double>(std::numeric_limits<float>::max());
        const float compiled_monitoring_gain = monitoring_gain_in_float32_range
                                                   ? static_cast<float>(monitoring_gain)
                                                   : 0.0F;
        require(report,
                monitoring_gain_in_float32_range &&
                    std::isfinite(compiled_monitoring_gain) &&
                    compiled_monitoring_gain > 0.0F,
                ContractIssueCode::invalid_value,
                "presentation.audition.monitoring_gain_linear.value",
                "monitoring gain must round to a finite positive Float32 value");

        require_canonical_zero(report, calibration.audition.fade_in_duration_s.value,
                               "presentation.audition.fade_in_duration_s.value");
        require_canonical_zero(report, calibration.audition.fade_out_duration_s.value,
                               "presentation.audition.fade_out_duration_s.value");
        const auto fade_in_frames = contract::resolve_frame_index(
            calibration.audition.fade_in_duration_s.value, kSourceRate);
        const auto fade_out_frames = contract::resolve_frame_index(
            calibration.audition.fade_out_duration_s.value, kSourceRate);
        require(report, fade_in_frames.has_value(), ContractIssueCode::invalid_value,
                "presentation.audition.fade_in_duration_s.value",
                "fade-in duration must resolve to an exact delivery-frame count");
        require(report, fade_out_frames.has_value(), ContractIssueCode::invalid_value,
                "presentation.audition.fade_out_duration_s.value",
                "fade-out duration must resolve to an exact delivery-frame count");
        require(report,
                fade_in_frames.has_value() && fade_out_frames.has_value() &&
                    *fade_in_frames <= audible_source_frames &&
                    *fade_out_frames <= audible_source_frames - *fade_in_frames,
                ContractIssueCode::inconsistent_semantics, "presentation.audition",
                "resolved audition fades must fit without overlap inside the audible "
                "horizon");

        if (!report.ok()) {
            return PresentationCalibrationCompileError{
                PresentationCalibrationCompileErrorCode::invalid_resolved_calibration,
                std::move(report),
            };
        }

        std::vector<AdmittedPresentationRoute> routes;
        routes.reserve(engine.routes.size());
        for (const auto &engine_route : engine.routes) {
            const auto *route = find_configured_route(calibration, engine_route.id);
            routes.push_back(AdmittedPresentationRoute{
                route->route_id, route->source_gain_linear,
                route->impulse_response_asset_id, route->impulse_response_gain_linear,
                route->wet_mix_01.value, engine_route.kind.value});
        }
        auto audition_routes = calibration.audition.selected_routes.value;
        return AdmittedPresentationCalibration{
            implemented_presentation_method_identities(),
            conditioning,
            std::move(routes),
            calibration.publication.calibration_gain_linear,
            audition_routes,
            MasteringSettings{audible_source_frames, *fade_in_frames, *fade_out_frames,
                              compiled_monitoring_gain},
            scenario.rates.capture,
            capture_frames_per_block,
            total_blocks,
            pre_audible_blocks,
        };
    }
};

} // namespace detail

PresentationCalibrationCompileResult
compile_presentation_calibration(const contract::PresentationCalibration &calibration,
                                 const contract::EngineSpec &engine,
                                 const contract::RenderScenario &scenario,
                                 const contract::ProvenanceLedger &provenance) {
    return detail::PresentationCalibrationCompiler::compile(calibration, engine,
                                                            scenario, provenance);
}

} // namespace engine_sim_offline::presentation
