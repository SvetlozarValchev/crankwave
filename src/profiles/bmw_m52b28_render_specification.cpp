#include "engine_sim_offline/profiles/bmw_m52b28_render_specification.hpp"

#include "engine_sim_offline/profiles/bmw_m52b28_operating_profile.hpp"

#include "presentation/presentation_method_registry.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace engine_sim_offline::profiles {
namespace {

constexpr std::string_view kRenderProvenanceDigestGrammar =
    "engine-sim-offline.bmw-m52b28-render-provenance-ledger-digest";
constexpr std::string_view kRenderProvenanceBundleId = "bmw-m52b28-render-provenance";
constexpr std::string_view kPresentationMethodClaimId =
    "bmw-m52b28-presentation-method-claim";
constexpr std::string_view kPresentationMonitoringClaimId =
    "bmw-m52b28-presentation-monitoring-claim";
constexpr std::string_view kRandomnessClaimId = "bmw-m52b28-render-randomness-claim";
constexpr std::string_view kResolutionIdPrefix = "bmw-m52b28-render-resolution-";
constexpr std::string_view kSeedNamespaceId = "baked.loaded_acceleration";

constexpr std::string_view kPressurePublicationEvidenceId =
    "physical-pressure-publication-method";
constexpr std::string_view kCoherentAuditionEvidenceId =
    "coherent-two-outlet-audition-method";
constexpr std::string_view kSourceMatrixEvidenceId =
    "bmw-m52b28-exhaust-acoustic-source-matrix";
constexpr std::string_view kRandomGeneratorEvidenceId =
    "pcg32-combustion-generator-method";
constexpr std::string_view kRandomDerivationEvidenceId =
    "combustion-seed-derivation-method";

enum class ResolutionAuthority : std::uint8_t {
    presentation_method,
    presentation_monitoring,
    randomness,
};

[[nodiscard]] std::string_view claim_id(ResolutionAuthority authority) noexcept {
    switch (authority) {
    case ResolutionAuthority::presentation_method:
        return kPresentationMethodClaimId;
    case ResolutionAuthority::presentation_monitoring:
        return kPresentationMonitoringClaimId;
    case ResolutionAuthority::randomness:
        return kRandomnessClaimId;
    }
    return {};
}

[[nodiscard]] contract::EvidenceSource
compiled_method_evidence(std::string_view evidence_id,
                         const contract::MethodIdentity &method) {
    return {
        std::string{evidence_id},
        "compiled-method-descriptor:" + method.id,
        std::string{"compiled repository implementation"},
        method.configuration_sha256,
        contract::RightsDisposition::permitted,
    };
}

class RenderProvenanceBuilder {
  public:
    explicit RenderProvenanceBuilder(contract::ProvenanceLedger simulation_provenance)
        : provenance_(std::move(simulation_provenance)) {
        const auto &presentation_methods =
            presentation::implemented_presentation_method_identities();
        const auto &source_matrix =
            contract::bmw_m52b28_exhaust_acoustic_source_matrix();
        const auto &generator = contract::pcg32_generator_method_identity();
        const auto &derivation = contract::component_seed_derivation_method_identity();

        provenance_.bundle.id = kRenderProvenanceBundleId;
        provenance_.evidence.push_back(compiled_method_evidence(
            kPressurePublicationEvidenceId,
            presentation_methods.calibrated_pressure_publication));
        provenance_.evidence.push_back(compiled_method_evidence(
            kCoherentAuditionEvidenceId,
            presentation_methods.coherent_two_outlet_audition));
        provenance_.evidence.push_back({
            std::string{kSourceMatrixEvidenceId},
            "docs/contracts/M5_BMW_EXHAUST_ACOUSTIC_SOURCE_MATRIX.md",
            std::string{"repository content"},
            source_matrix.sha256,
            contract::RightsDisposition::permitted,
        });
        provenance_.evidence.push_back(
            compiled_method_evidence(kRandomGeneratorEvidenceId, generator));
        provenance_.evidence.push_back(
            compiled_method_evidence(kRandomDerivationEvidenceId, derivation));

        provenance_.claims.push_back({
            std::string{kPresentationMethodClaimId},
            contract::ProvenanceOrigin::scenario,
            {
                {std::string{kPressurePublicationEvidenceId},
                 "complete compiled physical-pressure publication descriptor"},
                {std::string{kCoherentAuditionEvidenceId},
                 "complete compiled coherent-audition descriptor"},
                {std::string{kSourceMatrixEvidenceId},
                 "required routes, pressure calibration, buses, artifacts, and "
                 "explicit omissions"},
            },
            std::nullopt,
        });
        provenance_.claims.push_back({
            std::string{kPresentationMonitoringClaimId},
            contract::ProvenanceOrigin::artistic,
            {
                {std::string{kSourceMatrixEvidenceId},
                 "audition-only common listening transform boundary"},
            },
            std::nullopt,
        });
        provenance_.claims.push_back({
            std::string{kRandomnessClaimId},
            contract::ProvenanceOrigin::scenario,
            {
                {std::string{kRandomGeneratorEvidenceId},
                 "complete compiled deterministic combustion generator descriptor"},
                {std::string{kRandomDerivationEvidenceId},
                 "complete compiled per-cylinder seed derivation descriptor"},
            },
            std::nullopt,
        });
    }

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value>
    resolved(Value value, std::string parameter_path, ResolutionAuthority authority) {
        auto resolution_id =
            std::string{kResolutionIdPrefix} + std::to_string(next_resolution_++);
        provenance_.resolutions.push_back({
            resolution_id,
            std::move(parameter_path),
            contract::ResolutionMode::authored,
            std::string{claim_id(authority)},
            std::nullopt,
            {},
        });
        return {std::move(value), std::move(resolution_id)};
    }

    [[nodiscard]] std::string_view provenance_schema_id() const noexcept {
        return provenance_.schema_id;
    }

    [[nodiscard]] contract::ProvenanceLedger finish() && {
        provenance_.bundle.sha256 = contract::canonical_provenance_ledger_digest(
            provenance_, kRenderProvenanceDigestGrammar);
        return std::move(provenance_);
    }

  private:
    contract::ProvenanceLedger provenance_;
    std::uint32_t next_resolution_ = 1;
};

[[nodiscard]] contract::PresentationCalibration
make_presentation(RenderProvenanceBuilder &builder,
                  const contract::EngineSpec &engine) {
    const auto &methods = presentation::implemented_presentation_method_identities();

    contract::PresentationCalibration result;
    result.schema_version = 1;
    result.calibration_id = "bmw-m52b28-physical-exhaust-presentation";
    result.engine_profile_id =
        builder.resolved(engine.profile_id.value, "presentation.engine_profile_id",
                         ResolutionAuthority::presentation_method);
    result.methods = {
        builder.resolved(methods.calibrated_pressure_publication,
                         "presentation.methods.calibrated_pressure_publication",
                         ResolutionAuthority::presentation_method),
        builder.resolved(methods.coherent_two_outlet_audition,
                         "presentation.methods.coherent_two_outlet_audition",
                         ResolutionAuthority::presentation_method),
    };
    result.monitoring = {
        builder.resolved(0.5, "presentation.monitoring.gain_linear",
                         ResolutionAuthority::presentation_monitoring),
        builder.resolved(0.02, "presentation.monitoring.fade_in_duration_s",
                         ResolutionAuthority::presentation_monitoring),
        builder.resolved(0.02, "presentation.monitoring.fade_out_duration_s",
                         ResolutionAuthority::presentation_monitoring),
    };
    result.provenance_schema_id = builder.provenance_schema_id();
    return result;
}

[[nodiscard]] contract::ResolvedRandomnessPolicy
make_randomness(RenderProvenanceBuilder &builder) {
    return {
        builder.resolved(std::string{kSeedNamespaceId}, "randomness.seed_namespace_id",
                         ResolutionAuthority::randomness),
        builder.resolved(contract::pcg32_generator_method_identity(),
                         "randomness.generator", ResolutionAuthority::randomness),
        builder.resolved(contract::component_seed_derivation_method_identity(),
                         "randomness.derivation", ResolutionAuthority::randomness),
    };
}

void require_canonical_simulation_input(
    const contract::EngineSpec &engine,
    const contract::ProvenanceLedger &simulation_provenance) {
    const auto canonical_result = make_bmw_m52b28_operating_profile();
    const auto *canonical = std::get_if<BmwM52b28OperatingProfile>(&canonical_result);
    if (canonical == nullptr) {
        throw std::logic_error{
            "canonical BMW operating profile could not be constructed"};
    }
    if (engine != canonical->engine) {
        throw std::invalid_argument{
            "BMW render specification requires the canonical operating engine"};
    }
    if (!contract::validate(simulation_provenance).ok() ||
        !contract::validate(engine, simulation_provenance).ok()) {
        throw std::invalid_argument{
            "BMW render specification requires a valid resolved engine and "
            "simulation provenance ledger"};
    }
}

} // namespace

RenderSpecification
make_bmw_m52b28_render_specification(contract::EngineSpec engine,
                                     contract::ProvenanceLedger simulation_provenance) {
    require_canonical_simulation_input(engine, simulation_provenance);

    RenderProvenanceBuilder provenance_builder{std::move(simulation_provenance)};
    auto presentation = make_presentation(provenance_builder, engine);
    auto randomness = make_randomness(provenance_builder);
    auto provenance = std::move(provenance_builder).finish();

    if (!contract::validate(provenance).ok() ||
        !contract::validate(engine, provenance).ok() ||
        !contract::validate(randomness, provenance).ok()) {
        throw std::logic_error{
            "canonical BMW render specification construction violated its "
            "resolved contract"};
    }

    return {
        std::move(engine),
        std::move(presentation),
        std::move(randomness),
        std::move(provenance),
        contract::bmw_m52b28_exhaust_acoustic_source_matrix(),
    };
}

} // namespace engine_sim_offline::profiles
