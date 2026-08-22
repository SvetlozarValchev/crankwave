#include "crankwave/responsive/presentation_transfer.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "dsp/fixed_fft.hpp"
#include "presentation/presentation_asset_compiler.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace crankwave::responsive {
namespace {

[[nodiscard]] contract::ValidationReport
one_issue(const contract::ContractIssueCode code, std::string path,
          std::string message) {
    contract::ValidationReport report;
    report.add(code, std::move(path), std::move(message));
    return report;
}

void append_u64(std::vector<std::byte> &bytes, const std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & UINT64_C(0xff)));
    }
}

void append_string(std::vector<std::byte> &bytes, const std::string_view value) {
    append_u64(bytes, static_cast<std::uint64_t>(value.size()));
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

void append_digest(std::vector<std::byte> &bytes, const contract::Sha256Digest &value) {
    for (const auto byte : value.bytes) {
        bytes.push_back(static_cast<std::byte>(byte));
    }
}

void append_f64(std::vector<std::byte> &bytes, const double value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value));
}

void append_complex_f64le(std::vector<std::byte> &bytes,
                          const std::span<const std::complex<double>> values) {
    if (values.size() >
        std::numeric_limits<std::size_t>::max() / (2U * sizeof(double))) {
        throw std::length_error{"responsive transfer spectrum is too large"};
    }
    bytes.reserve(values.size() * 2U * sizeof(double));
    for (const auto value : values) {
        append_u64(bytes, std::bit_cast<std::uint64_t>(value.real()));
        append_u64(bytes, std::bit_cast<std::uint64_t>(value.imag()));
    }
}

[[nodiscard]] const contract::AudioAssetSpec *
find_asset(const contract::PresentationCalibration &presentation,
           const contract::AudioAssetId id) {
    const auto found =
        std::ranges::find(presentation.assets, id, &contract::AudioAssetSpec::id);
    return found == presentation.assets.end() ? nullptr : &*found;
}

[[nodiscard]] const compile::detail::VerifiedEngineAsset *
find_payload(const compile::detail::ResolvedEnginePackage &engine,
             const contract::AudioAssetSpec &asset) {
    const auto found = std::ranges::find_if(engine.assets, [&](const auto &value) {
        return value.kind == compile::AssetKind::audio &&
               value.asset_id == asset.semantic_id.value;
    });
    return found == engine.assets.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::RouteSpec *
find_engine_route(const contract::EngineSpec &engine, const contract::RouteId id) {
    const auto found = std::ranges::find(engine.routes, id, &contract::RouteSpec::id);
    return found == engine.routes.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::RoutePresentation *
find_presentation_route(const contract::PresentationCalibration &presentation,
                        const contract::RouteId id) {
    const auto found = std::ranges::find(presentation.routes, id,
                                         &contract::RoutePresentation::route_id);
    return found == presentation.routes.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::SourceRouteRequirement *
find_route_requirement(const contract::SourceMatrixContract &matrix,
                       const std::string_view semantic_id) {
    const auto found =
        std::ranges::find(matrix.required_source_routes, semantic_id,
                          &contract::SourceRouteRequirement::semantic_id);
    return found == matrix.required_source_routes.end() ? nullptr : &*found;
}

[[nodiscard]] std::string
audition_bus_id(const compile::detail::ResolvedEnginePackage &engine) {
    const auto found = std::ranges::find(
        engine.audio_buses, contract::OutputBusKind::master_engine_audition,
        &compile::detail::ResolvedAudioBusDescriptor::kind);
    return found == engine.audio_buses.end() ? std::string{} : found->authored_id;
}

[[nodiscard]] contract::Sha256Digest
transfer_identity(const ResponsiveCompiledTransfer &transfer) {
    std::vector<std::byte> preimage;
    append_string(preimage, kResponsivePresentationIdentityMethodId);
    append_string(preimage, "transfer");
    append_u64(preimage, static_cast<std::uint64_t>(transfer.shape));
    append_u64(preimage, transfer.fft_size);
    append_u64(preimage, transfer.coefficient_count);
    append_u64(preimage, transfer.partition_frame_count);
    append_u64(preimage, transfer.partition_count);
    append_string(preimage, transfer.spectrum_encoding);
    append_u64(preimage, static_cast<std::uint64_t>(transfer.spectrum_bytes.size()));
    append_digest(preimage, transfer.spectrum_sha256);
    return contract::sha256(preimage);
}

[[nodiscard]] contract::Sha256Digest
presentation_identity(const ResponsiveCompiledPresentation &presentation) {
    std::vector<std::byte> preimage;
    append_string(preimage, kResponsivePresentationIdentityMethodId);
    append_string(preimage, "presentation");
    append_string(preimage, presentation.engine_id);
    append_string(preimage, presentation.audition_bus_id);
    append_f64(preimage, presentation.captured_to_source_scale);
    append_f64(preimage, presentation.master_volume_linear);
    append_u64(preimage, static_cast<std::uint64_t>(presentation.transfers.size()));
    for (const auto &transfer : presentation.transfers) {
        append_digest(preimage, transfer.identity_sha256);
    }
    append_u64(preimage, static_cast<std::uint64_t>(presentation.routes.size()));
    for (const auto &route : presentation.routes) {
        append_string(preimage, route.dry_bus_id);
        append_string(preimage, route.source_route_id);
        append_string(preimage, route.impulse_response_asset_id);
        append_digest(preimage, route.impulse_response_payload_sha256);
        append_f64(preimage, route.impulse_response_gain_linear);
        append_f64(preimage, route.wet_mix_01);
        append_u64(preimage, static_cast<std::uint64_t>(route.transfer_index));
    }
    return contract::sha256(preimage);
}

[[nodiscard]] bool same_transfer(const ResponsiveCompiledTransfer &left,
                                 const ResponsiveCompiledTransfer &right) {
    return left.shape == right.shape && left.fft_size == right.fft_size &&
           left.coefficient_count == right.coefficient_count &&
           left.partition_frame_count == right.partition_frame_count &&
           left.partition_count == right.partition_count &&
           left.spectrum_encoding == right.spectrum_encoding &&
           left.spectrum_sha256 == right.spectrum_sha256 &&
           left.spectrum_bytes == right.spectrum_bytes;
}

[[nodiscard]] std::variant<ResponsiveCompiledTransfer, contract::ValidationReport>
compile_transfer(const contract::AudioAssetSpec &asset,
                 const compile::detail::VerifiedEngineAsset &payload,
                 const contract::PresentationCalibration &presentation,
                 const contract::RoutePresentation &route) {
    auto asset_result = presentation::compile_presentation_asset(
        asset, {asset.id, payload.bytes},
        presentation.methods.impulse_response_conversion.value,
        route.impulse_response_gain_linear);
    if (std::holds_alternative<presentation::PresentationAssetCompileError>(
            asset_result)) {
        return one_issue(contract::ContractIssueCode::unsupported_value,
                         "presentation.assets." + asset.semantic_id.value,
                         "the selected IR cannot be compiled by the resolved native "
                         "presentation method");
    }
    auto compiled_asset =
        std::get<presentation::CompiledPresentationAsset>(std::move(asset_result));
    auto kernel_result = presentation::compile_presentation_convolution_kernel(
        compiled_asset, presentation.methods.convolution.value);
    if (std::holds_alternative<presentation::PresentationConvolutionKernelCompileError>(
            kernel_result)) {
        return one_issue(
            contract::ContractIssueCode::unsupported_value, "presentation.routes",
            "the selected IR cannot produce the resolved native convolution "
            "kernel");
    }
    auto kernel = std::get<presentation::CompiledPresentationConvolutionKernel>(
        std::move(kernel_result));

    ResponsiveCompiledTransfer result;
    if (kernel.kernel() != nullptr) {
        result.shape = ResponsiveTransferShape::fixed_overlap_save;
        result.fft_size = dsp::FixedFftLimits::transform_length;
        result.coefficient_count = dsp::FixedConvolutionKernel::coefficient_count;
        result.partition_frame_count = 0U;
        result.partition_count = 1U;
        append_complex_f64le(result.spectrum_bytes, kernel.kernel()->spectrum());
    } else if (kernel.partitioned_kernel() != nullptr) {
        result.shape = ResponsiveTransferShape::uniform_partitioned_overlap_save;
        result.fft_size = dsp::PartitionedConvolutionLimits::transform_length;
        result.coefficient_count = kernel.partitioned_kernel()->coefficient_count();
        result.partition_frame_count =
            dsp::PartitionedConvolutionLimits::partition_frame_count;
        result.partition_count = kernel.partitioned_kernel()->partition_count();
        append_complex_f64le(result.spectrum_bytes,
                             kernel.partitioned_kernel()->spectra());
    } else {
        return one_issue(contract::ContractIssueCode::inconsistent_semantics,
                         "presentation.routes",
                         "compiled convolution kernel has no runtime shape");
    }
    result.spectrum_sha256 = contract::sha256(result.spectrum_bytes);
    if (result.spectrum_sha256 !=
            kernel.spectrum_complex_f64le_identity().payload_sha256 ||
        result.spectrum_bytes.size() !=
            kernel.spectrum_complex_f64le_identity().byte_count) {
        return one_issue(
            contract::ContractIssueCode::inconsistent_semantics, "presentation.routes",
            "serialized responsive transfer disagrees with its compiled kernel "
            "identity");
    }
    result.identity_sha256 = transfer_identity(result);
    return result;
}

} // namespace

ResponsivePresentationCompileResult compile_responsive_presentation_transfer(
    const compile::CompiledScenario &compiled_scenario) {
    const auto inputs =
        compile::detail::CompiledScenarioViewAccess::inputs(compiled_scenario);
    const auto &engine_package = inputs.engine;
    const auto &presentation = engine_package.presentation;
    const auto &engine = engine_package.engine;

    ResponsiveCompiledPresentation result;
    result.engine_id = engine.engine_id.value;
    result.audition_bus_id = audition_bus_id(engine_package);
    const double publication_gain =
        presentation.publication.calibration_gain_linear.value;
    result.master_volume_linear = presentation.audition.volume_linear.value;
    if (result.engine_id.empty() || result.audition_bus_id.empty() ||
        !std::isfinite(publication_gain) || !(publication_gain > 0.0) ||
        !std::isfinite(result.master_volume_linear) ||
        !(result.master_volume_linear > 0.0)) {
        return one_issue(contract::ContractIssueCode::invalid_value, "presentation",
                         "responsive presentation calibration is invalid");
    }
    result.captured_to_source_scale = 1.0 / publication_gain;
    if (!std::isfinite(result.captured_to_source_scale) ||
        !(result.captured_to_source_scale > 0.0)) {
        return one_issue(contract::ContractIssueCode::invalid_value,
                         "presentation.publication",
                         "publication gain has no finite inverse");
    }

    const auto &ordered_route_ids = presentation.audition.selected_routes.value;
    if (ordered_route_ids.empty() ||
        ordered_route_ids.size() != presentation.routes.size()) {
        return one_issue(
            contract::ContractIssueCode::inconsistent_shape,
            "presentation.audition.selected_routes",
            "responsive audition must select every configured route exactly once");
    }
    result.routes.reserve(ordered_route_ids.size());
    result.audition_dry_bus_order.reserve(ordered_route_ids.size());

    for (const auto route_id : ordered_route_ids) {
        const auto *route = find_presentation_route(presentation, route_id);
        const auto *engine_route = find_engine_route(engine, route_id);
        if (route == nullptr || engine_route == nullptr ||
            !route->impulse_response_asset_id.has_value()) {
            return one_issue(contract::ContractIssueCode::dangling_reference,
                             "presentation.routes",
                             "responsive presentation route or selected IR is absent");
        }
        const auto *requirement = find_route_requirement(
            inputs.scenario.source_matrix, engine_route->semantic_id.value);
        if (requirement == nullptr || requirement->artifact_roles.size() != 3U ||
            requirement->artifact_roles.front() !=
                engine_route->semantic_id.value + ".dry") {
            return one_issue(contract::ContractIssueCode::inconsistent_semantics,
                             "scenario.source_matrix.required_source_routes",
                             "responsive route lacks its canonical dry-bus role");
        }
        const auto *asset = find_asset(presentation, *route->impulse_response_asset_id);
        if (asset == nullptr) {
            return one_issue(contract::ContractIssueCode::dangling_reference,
                             "presentation.routes",
                             "responsive route references an absent IR asset");
        }
        const auto *payload = find_payload(engine_package, *asset);
        if (payload == nullptr ||
            contract::sha256(payload->bytes) != asset->content_sha256.value) {
            return one_issue(contract::ContractIssueCode::invalid_value,
                             "presentation.assets." + asset->semantic_id.value,
                             "responsive IR payload is absent or hash-mismatched");
        }
        if (!std::isfinite(route->impulse_response_gain_linear.value) ||
            route->impulse_response_gain_linear.value < 0.0 ||
            !std::isfinite(route->wet_mix_01.value) || route->wet_mix_01.value < 0.0 ||
            route->wet_mix_01.value > 1.0) {
            return one_issue(contract::ContractIssueCode::invalid_value,
                             "presentation.routes." + engine_route->semantic_id.value,
                             "responsive route transfer calibration is invalid");
        }

        auto transfer_result = compile_transfer(*asset, *payload, presentation, *route);
        if (const auto *report =
                std::get_if<contract::ValidationReport>(&transfer_result)) {
            return *report;
        }
        auto transfer =
            std::get<ResponsiveCompiledTransfer>(std::move(transfer_result));
        const auto existing =
            std::ranges::find_if(result.transfers, [&](const auto &candidate) {
                return same_transfer(candidate, transfer);
            });
        std::size_t transfer_index = 0U;
        if (existing == result.transfers.end()) {
            transfer_index = result.transfers.size();
            result.transfers.push_back(std::move(transfer));
        } else {
            transfer_index =
                static_cast<std::size_t>(existing - result.transfers.begin());
        }

        result.audition_dry_bus_order.push_back(requirement->artifact_roles.front());
        result.routes.push_back({
            requirement->artifact_roles.front(),
            engine_route->semantic_id.value,
            asset->semantic_id.value,
            asset->content_sha256.value,
            route->impulse_response_gain_linear.value,
            route->wet_mix_01.value,
            transfer_index,
        });
    }

    const std::unordered_set<std::string> unique_dry_buses{
        result.audition_dry_bus_order.begin(), result.audition_dry_bus_order.end()};
    if (unique_dry_buses.size() != result.audition_dry_bus_order.size() ||
        result.routes.size() != result.audition_dry_bus_order.size()) {
        return one_issue(contract::ContractIssueCode::duplicate_identity,
                         "presentation.audition",
                         "responsive dry route order contains duplicates");
    }
    result.identity_sha256 = presentation_identity(result);
    return result;
}

} // namespace crankwave::responsive
