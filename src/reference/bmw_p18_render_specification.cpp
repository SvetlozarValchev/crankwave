#include "reference/bmw_p18_render_specification.hpp"

#include "presentation/presentation_method_registry.hpp"
#include "reference/p18_reference_catalog.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::reference {
namespace {

constexpr std::string_view kCombinedProvenanceDigestGrammar =
    "engine-sim-offline.bmw-public-render-provenance-ledger-digest.v1";
constexpr std::string_view kCombinedProvenanceBundleId =
    "bmw-m52b28-public-render-provenance-v1";
constexpr std::string_view kPresentationClaimId =
    "bmw-m52b28-public-render-presentation-claim";
constexpr std::string_view kResolutionIdPrefix = "bmw-m52b28-public-render-resolution-";
constexpr std::string_view kFixtureLocatorPrefix = "reference/fixtures/bmw-m52b28-p18/";

[[nodiscard]] double binary64(P18ExpectedBinary64 expected) noexcept {
    return std::bit_cast<double>(expected.expected_ieee754_bits);
}

[[nodiscard]] const P18ExpectedLineageFile &
expected_lineage(const P18ReferenceCatalogV1 &catalog, P18ReferenceLineageFile file) {
    const auto found = std::ranges::find(catalog.expected_lineage_files, file,
                                         &P18ExpectedLineageFile::file);
    if (found == catalog.expected_lineage_files.end()) {
        throw std::logic_error{"P1.8 lineage catalog is incomplete"};
    }
    return *found;
}

[[nodiscard]] const P18ExpectedRoute &
expected_route(const P18ReferenceCatalogV1 &catalog, P18ReferenceRoute route) {
    const auto found =
        std::ranges::find(catalog.expected_routes, route, &P18ExpectedRoute::route);
    if (found == catalog.expected_routes.end()) {
        throw std::logic_error{"P1.8 route catalog is incomplete"};
    }
    return *found;
}

class CombinedProvenanceBuilder {
  public:
    CombinedProvenanceBuilder(contract::ProvenanceLedger simulation_provenance,
                              const P18ReferenceCatalogV1 &catalog,
                              const contract::Sha256Digest &configured_ir_sha256)
        : provenance_(std::move(simulation_provenance)) {
        const auto &presentation = catalog.expected_presentation;
        const auto &algorithm = expected_lineage(
            catalog, P18ReferenceLineageFile::renderer_algorithm_record);
        const auto &configured_ir =
            expected_lineage(catalog, P18ReferenceLineageFile::configured_ir_input);

        provenance_.bundle.id = kCombinedProvenanceBundleId;
        provenance_.evidence.push_back({
            std::string{presentation.expected_algorithm_record_evidence_source_id},
            std::string{kFixtureLocatorPrefix} +
                std::string{algorithm.expected_relative_path},
            std::string{"repository content"},
            algorithm.expected_sha256,
            contract::RightsDisposition::local_evaluation_only,
        });
        provenance_.evidence.push_back({
            std::string{
                presentation.expected_configured_ir_media.expected_evidence_source_id},
            std::string{kFixtureLocatorPrefix} +
                std::string{configured_ir.expected_relative_path},
            std::string{"repository content"},
            configured_ir_sha256,
            contract::RightsDisposition::local_evaluation_only,
        });
        provenance_.claims.push_back({
            std::string{kPresentationClaimId},
            contract::ProvenanceOrigin::reference_fixture,
            {
                {
                    std::string{
                        presentation.expected_algorithm_record_evidence_source_id},
                    "complete file: frozen presentation calibration and methods",
                },
                {
                    std::string{presentation.expected_configured_ir_media
                                    .expected_evidence_source_id},
                    "complete file: configured static impulse response",
                },
            },
            std::nullopt,
        });
    }

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value> resolved(Value value,
                                                          std::string parameter_path) {
        auto resolution_id =
            std::string{kResolutionIdPrefix} + std::to_string(next_resolution_++);
        provenance_.resolutions.push_back({
            resolution_id,
            std::move(parameter_path),
            contract::ResolutionMode::authored,
            std::string{kPresentationClaimId},
            std::nullopt,
            {},
        });
        return {std::move(value), std::move(resolution_id)};
    }

    [[nodiscard]] contract::ProvenanceLedger finish() && {
        provenance_.bundle.sha256 = contract::canonical_provenance_ledger_digest(
            provenance_, kCombinedProvenanceDigestGrammar);
        return std::move(provenance_);
    }

  private:
    contract::ProvenanceLedger provenance_;
    std::uint32_t next_resolution_ = 1;
};

[[nodiscard]] contract::PresentationCalibration make_presentation(
    CombinedProvenanceBuilder &builder, const contract::EngineSpec &engine,
    std::string_view provenance_schema_id, const P18ReferenceCatalogV1 &catalog,
    const contract::Sha256Digest &configured_ir_sha256) {
    const auto &policy = catalog.expected_presentation;
    const auto &media = policy.expected_configured_ir_media;
    const auto &scalars = policy.expected_scalars;
    const auto &methods = presentation::implemented_presentation_method_identities();

    contract::PresentationCalibration result;
    result.schema_version = 2;
    result.calibration_id = std::string{catalog.expected_presentation_calibration_id};
    result.engine_profile_id =
        builder.resolved(engine.profile_id.value, "presentation.engine_profile_id");
    result.methods = {
        builder.resolved(methods.reconstruction, "presentation.methods.reconstruction"),
        builder.resolved(methods.conditioning, "presentation.methods.conditioning"),
        builder.resolved(methods.impulse_response_conversion,
                         "presentation.methods.impulse_response_conversion"),
        builder.resolved(methods.convolution, "presentation.methods.convolution"),
        builder.resolved(methods.publication, "presentation.methods.publication"),
        builder.resolved(methods.audition_mix, "presentation.methods.audition_mix"),
    };
    result.conditioning = {
        builder.resolved(binary64(scalars.expected_jitter_scale),
                         "presentation.conditioning.jitter_scale"),
        builder.resolved(binary64(scalars.expected_jitter_modulation_cutoff_hz),
                         "presentation.conditioning.jitter_modulation_cutoff_hz"),
        builder.resolved(binary64(scalars.expected_derivative_mix_01),
                         "presentation.conditioning.derivative_mix_01"),
        builder.resolved(binary64(scalars.expected_air_noise_mix_01),
                         "presentation.conditioning.air_noise_mix_01"),
        builder.resolved(binary64(scalars.expected_air_noise_cutoff_hz),
                         "presentation.conditioning.air_noise_cutoff_hz"),
    };
    result.assets.push_back({
        media.expected_asset_id,
        builder.resolved(std::string{media.expected_semantic_id},
                         "presentation.assets.smooth-39.semantic_id"),
        builder.resolved(std::string{media.expected_evidence_source_id},
                         "presentation.assets.smooth-39.evidence_source_id"),
        builder.resolved(configured_ir_sha256,
                         "presentation.assets.smooth-39.content_sha256"),
        builder.resolved(
            contract::AudioMediaContract{
                media.expected_encoding,
                media.expected_channel_layout,
                media.expected_sample_rate,
                media.expected_frame_count,
            },
            "presentation.assets.smooth-39.media"),
    });
    for (const auto &route : catalog.expected_routes) {
        const auto prefix =
            "presentation.routes." + std::string{route.expected_semantic_id};
        result.routes.push_back({
            route.expected_route_id,
            media.expected_asset_id,
            builder.resolved(binary64(scalars.expected_impulse_response_gain_linear),
                             prefix + ".impulse_response_gain_linear"),
            builder.resolved(binary64(scalars.expected_wet_mix_01),
                             prefix + ".wet_mix_01"),
        });
    }
    result.publication.calibration_gain_linear =
        builder.resolved(binary64(scalars.expected_publication_calibration_gain_linear),
                         "presentation.publication.calibration_gain_linear");
    result.audition = {
        builder.resolved(
            std::vector<contract::RouteId>{
                expected_route(catalog, policy.expected_audition_route_order[0])
                    .expected_route_id,
                expected_route(catalog, policy.expected_audition_route_order[1])
                    .expected_route_id,
            },
            "presentation.audition.selected_routes"),
        builder.resolved(binary64(scalars.expected_audition_monitoring_gain_linear),
                         "presentation.audition.monitoring_gain_linear"),
        builder.resolved(binary64(scalars.expected_audition_fade_in_duration_s),
                         "presentation.audition.fade_in_duration_s"),
        builder.resolved(binary64(scalars.expected_audition_fade_out_duration_s),
                         "presentation.audition.fade_out_duration_s"),
    };
    result.provenance_schema_id = provenance_schema_id;
    return result;
}

[[nodiscard]] contract::ResolvedRandomnessPolicy
make_randomness(CombinedProvenanceBuilder &builder) {
    return {
        builder.resolved(std::string{"baked.loaded_acceleration"},
                         "randomness.seed_namespace_id"),
        builder.resolved(contract::pcg32_generator_method_identity(),
                         "randomness.generator"),
        builder.resolved(contract::component_seed_derivation_method_identity(),
                         "randomness.derivation"),
    };
}

} // namespace

RenderSpecification make_bmw_p18_render_specification(
    contract::EngineSpec engine, contract::ProvenanceLedger simulation_provenance,
    std::vector<std::byte> verified_configured_ir_wave_bytes) {
    const auto &catalog = p18_reference_catalog_v1();
    if (engine.engine_id.value != catalog.expected_engine_id) {
        throw std::invalid_argument{
            "P1.8 BMW presentation requires the BMW M52B28 engine identity"};
    }

    const auto &expected_ir =
        expected_lineage(catalog, P18ReferenceLineageFile::configured_ir_input);
    if (verified_configured_ir_wave_bytes.size() != expected_ir.expected_byte_count) {
        throw std::invalid_argument{
            "P1.8 configured IR byte count differs from the immutable catalog"};
    }
    const auto configured_ir_sha256 =
        contract::sha256(verified_configured_ir_wave_bytes);
    if (configured_ir_sha256 != expected_ir.expected_sha256) {
        throw std::invalid_argument{
            "P1.8 configured IR digest differs from the immutable catalog"};
    }

    const auto provenance_schema_id = simulation_provenance.schema_id;
    CombinedProvenanceBuilder builder{
        std::move(simulation_provenance),
        catalog,
        configured_ir_sha256,
    };
    auto presentation = make_presentation(builder, engine, provenance_schema_id,
                                          catalog, configured_ir_sha256);
    auto randomness = make_randomness(builder);
    return {
        std::move(engine),
        std::move(presentation),
        std::move(randomness),
        std::move(builder).finish(),
        contract::bmw_m52b28_reference_source_matrix_v1(),
        {
            {
                catalog.expected_presentation.expected_configured_ir_media
                    .expected_asset_id,
                std::move(verified_configured_ir_wave_bytes),
            },
        },
    };
}

} // namespace engine_sim_offline::reference
