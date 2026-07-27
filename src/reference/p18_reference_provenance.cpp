#include "reference/p18_reference_provenance.hpp"

#include "contract/sha256_stream.hpp"
#include "reference/p18_reference_catalog.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::reference {
namespace {

constexpr std::string_view kDigestGrammar =
    "engine-sim-offline.p18-provenance-ledger-digest.v1";
constexpr std::string_view kSchemaId = "engine-sim-offline.p18-reference-provenance.v1";
constexpr std::string_view kBundleId = "bmw-m52b28-p18-reference-provenance-v1";
constexpr std::string_view kFixtureClaimId = "p18-reference-fixture-claim";
constexpr std::string_view kRendererClaimId = "p18-presentation-renderer-record-claim";
constexpr std::string_view kConfiguredIrClaimId = "p18-configured-ir-claim";
constexpr std::string_view kFixtureLocatorPrefix = "reference/fixtures/bmw-m52b28-p18/";
constexpr std::string_view kResolutionIdPrefix = "p18-reference-resolution/";

struct EvidenceDefinition {
    P18ReferenceLineageFile file;
    std::string_view id;
    std::string_view relative_path;
};

constexpr std::array<EvidenceDefinition, kP18ReferenceLineageFileCount>
    kEvidenceDefinitions{{
        {
            P18ReferenceLineageFile::manifest,
            "p18-fixture-manifest",
            "manifest.json",
        },
        {
            P18ReferenceLineageFile::parity_evidence,
            "p18-parity-evidence",
            "reference-parity.bin",
        },
        {
            P18ReferenceLineageFile::audit_input,
            "p18-audit-input",
            "reference-audit.bin",
        },
        {
            P18ReferenceLineageFile::component_seed_input,
            "p18-component-seed-input",
            "component-seeds.bin",
        },
        {
            P18ReferenceLineageFile::renderer_algorithm_record,
            "p18-presentation-renderer-record",
            "P18_PRESENTATION_RENDERER.md",
        },
        {
            P18ReferenceLineageFile::configured_ir_input,
            "smooth-39-ir",
            "presentation/smooth_39.wav",
        },
        {
            P18ReferenceLineageFile::kernel_oracle_comparator,
            "p18-kernel-comparator",
            "presentation/smooth_39-192000hz-volume-0p001-f64le.bin",
        },
    }};

struct ResolutionDefinition {
    std::string_view parameter_path;
    std::string_view claim_id;
};

constexpr std::array<ResolutionDefinition, 28> kResolutionDefinitions{{
    {"presentation.engine_profile_id", kRendererClaimId},
    {"presentation.methods.reconstruction", kRendererClaimId},
    {"presentation.methods.conditioning", kRendererClaimId},
    {"presentation.methods.impulse_response_conversion", kRendererClaimId},
    {"presentation.methods.convolution", kRendererClaimId},
    {"presentation.methods.publication", kRendererClaimId},
    {"presentation.methods.audition_mix", kRendererClaimId},
    {"presentation.algorithm_record.semantic_id", kRendererClaimId},
    {"presentation.algorithm_record.evidence_source_id", kRendererClaimId},
    {"presentation.algorithm_record.content_sha256", kRendererClaimId},
    {"presentation.conditioning.jitter_scale", kRendererClaimId},
    {"presentation.conditioning.jitter_modulation_cutoff_hz", kRendererClaimId},
    {"presentation.conditioning.derivative_mix_01", kRendererClaimId},
    {"presentation.conditioning.air_noise_mix_01", kRendererClaimId},
    {"presentation.conditioning.air_noise_cutoff_hz", kRendererClaimId},
    {"presentation.assets.smooth-39.semantic_id", kConfiguredIrClaimId},
    {"presentation.assets.smooth-39.evidence_source_id", kConfiguredIrClaimId},
    {"presentation.assets.smooth-39.content_sha256", kConfiguredIrClaimId},
    {"presentation.assets.smooth-39.media", kConfiguredIrClaimId},
    {"presentation.routes.exhaust.reference.0.impulse_response_gain_linear",
     kRendererClaimId},
    {"presentation.routes.exhaust.reference.0.wet_mix_01", kRendererClaimId},
    {"presentation.routes.exhaust.reference.1.impulse_response_gain_linear",
     kRendererClaimId},
    {"presentation.routes.exhaust.reference.1.wet_mix_01", kRendererClaimId},
    {"presentation.publication.calibration_gain_linear", kRendererClaimId},
    {"presentation.audition.selected_routes", kRendererClaimId},
    {"presentation.audition.monitoring_gain_linear", kRendererClaimId},
    {"presentation.audition.fade_in_duration_s", kRendererClaimId},
    {"presentation.audition.fade_out_duration_s", kRendererClaimId},
}};

class DigestWriter {
  public:
    void byte(std::uint8_t value) noexcept {
        const std::byte encoded{value};
        stream_.update(std::span<const std::byte>{&encoded, 1});
    }

    void u32(std::uint32_t value) noexcept {
        std::array<std::byte, 4> encoded{};
        for (std::uint32_t index = 0; index < encoded.size(); ++index) {
            encoded[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        stream_.update(encoded);
    }

    void u64(std::uint64_t value) noexcept {
        std::array<std::byte, 8> encoded{};
        for (std::uint32_t index = 0; index < encoded.size(); ++index) {
            encoded[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        stream_.update(encoded);
    }

    void string(std::string_view value) noexcept {
        u64(static_cast<std::uint64_t>(value.size()));
        stream_.update(
            std::as_bytes(std::span<const char>{value.data(), value.size()}));
    }

    void digest(const contract::Sha256Digest &value) noexcept {
        stream_.update(std::as_bytes(
            std::span<const std::uint8_t>{value.bytes.data(), value.bytes.size()}));
    }

    template <class T, class Write>
    void optional(const std::optional<T> &value, Write write) noexcept {
        byte(value.has_value() ? 1U : 0U);
        if (value.has_value()) {
            write(*value);
        }
    }

    [[nodiscard]] contract::Sha256Digest finish() const noexcept {
        return stream_.finish();
    }

  private:
    contract::detail::Sha256Stream stream_;
};

void write_method(DigestWriter &writer,
                  const contract::MethodIdentity &method) noexcept {
    writer.string(method.id);
    writer.u32(method.version);
    writer.digest(method.configuration_sha256);
}

void write_uncertainty(DigestWriter &writer,
                       const contract::UncertaintyStatement &uncertainty) noexcept {
    writer.optional<double>(
        uncertainty.standard_uncertainty, [&](double value) noexcept {
            // The contract treats both signed zero representations as the same
            // uncertainty. Canonical bytes therefore use positive zero for either.
            const auto bits =
                value == 0.0 ? UINT64_C(0) : std::bit_cast<std::uint64_t>(value);
            writer.u64(bits);
        });
    writer.string(uncertainty.method);
}

[[nodiscard]] const P18ExpectedLineageFile &
expected_lineage_file(P18ReferenceLineageFile file) {
    const auto &catalog = p18_reference_catalog_v1();
    const auto index = static_cast<std::size_t>(file);
    if (index >= catalog.expected_lineage_files.size() ||
        catalog.expected_lineage_files[index].file != file) {
        throw std::logic_error{"P1.8 lineage catalog order changed"};
    }
    return catalog.expected_lineage_files[index];
}

void require_static_definitions_match_catalog() {
    const auto &catalog = p18_reference_catalog_v1();
    for (std::size_t index = 0; index < kEvidenceDefinitions.size(); ++index) {
        const auto &definition = kEvidenceDefinitions[index];
        const auto &expected = expected_lineage_file(definition.file);
        if (static_cast<std::size_t>(definition.file) != index ||
            definition.relative_path != expected.expected_relative_path) {
            throw std::logic_error{
                "P1.8 provenance lineage definition differs from the catalog"};
        }
    }
    if (kEvidenceDefinitions[4].id !=
            catalog.expected_presentation
                .expected_algorithm_record_evidence_source_id ||
        kEvidenceDefinitions[5].id !=
            catalog.expected_presentation.expected_configured_ir_media
                .expected_evidence_source_id) {
        throw std::logic_error{
            "P1.8 provenance evidence IDs differ from presentation inputs"};
    }
}

[[nodiscard]] contract::ProvenanceClaim
fixture_claim(const std::vector<contract::EvidenceSource> &evidence) {
    contract::ProvenanceClaim claim;
    claim.id = kFixtureClaimId;
    claim.origin = contract::ProvenanceOrigin::reference_fixture;
    claim.citations.reserve(evidence.size());
    for (const auto &source : evidence) {
        claim.citations.push_back({source.id, "complete-file"});
    }
    return claim;
}

[[nodiscard]] contract::ProvenanceClaim
single_file_claim(std::string_view claim_id, std::string_view evidence_id) {
    return {
        std::string{claim_id},
        contract::ProvenanceOrigin::reference_fixture,
        {{std::string{evidence_id}, "complete-file"}},
        std::nullopt,
    };
}

} // namespace

const contract::ProvenanceLedger &P18ReferenceProvenance::ledger() const noexcept {
    return ledger_;
}

std::string_view
P18ReferenceProvenance::resolution_id(std::string_view parameter_path) const {
    for (const auto &resolution : ledger_.resolutions) {
        if (resolution.parameter_path == parameter_path) {
            return resolution.id;
        }
    }
    throw std::out_of_range{"P1.8 provenance has no resolution for parameter path"};
}

contract::Sha256Digest canonical_p18_reference_provenance_digest(
    const contract::ProvenanceLedger &ledger) noexcept {
    DigestWriter writer;
    writer.string(kDigestGrammar);
    writer.string(ledger.schema_id);
    writer.string(ledger.bundle.id);

    writer.u64(static_cast<std::uint64_t>(ledger.evidence.size()));
    for (const auto &evidence : ledger.evidence) {
        writer.string(evidence.id);
        writer.string(evidence.locator);
        writer.optional<std::string>(
            evidence.revision,
            [&](const std::string &revision) noexcept { writer.string(revision); });
        writer.optional<contract::Sha256Digest>(
            evidence.content_sha256,
            [&](const contract::Sha256Digest &digest) noexcept {
                writer.digest(digest);
            });
        writer.byte(static_cast<std::uint8_t>(evidence.rights));
    }

    writer.u64(static_cast<std::uint64_t>(ledger.claims.size()));
    for (const auto &claim : ledger.claims) {
        writer.string(claim.id);
        writer.byte(static_cast<std::uint8_t>(claim.origin));
        writer.u64(static_cast<std::uint64_t>(claim.citations.size()));
        for (const auto &citation : claim.citations) {
            writer.string(citation.evidence_id);
            writer.string(citation.claim_locator);
        }
        writer.optional<contract::UncertaintyStatement>(
            claim.uncertainty,
            [&](const contract::UncertaintyStatement &uncertainty) noexcept {
                write_uncertainty(writer, uncertainty);
            });
    }

    writer.u64(static_cast<std::uint64_t>(ledger.resolutions.size()));
    for (const auto &resolution : ledger.resolutions) {
        writer.string(resolution.id);
        writer.string(resolution.parameter_path);
        writer.byte(static_cast<std::uint8_t>(resolution.mode));
        writer.string(resolution.claim_id);
        writer.optional<contract::MethodIdentity>(
            resolution.method, [&](const contract::MethodIdentity &method) noexcept {
                write_method(writer, method);
            });
        writer.u64(
            static_cast<std::uint64_t>(resolution.dependency_parameter_paths.size()));
        for (const auto &dependency : resolution.dependency_parameter_paths) {
            writer.string(dependency);
        }
    }
    return writer.finish();
}

P18ReferenceProvenance
make_p18_reference_provenance(const P18VerifiedReferenceLineage &lineage) {
    require_static_definitions_match_catalog();

    contract::ProvenanceLedger ledger;
    ledger.schema_id = kSchemaId;
    ledger.bundle.id = kBundleId;
    ledger.evidence.reserve(kEvidenceDefinitions.size());
    for (const auto &definition : kEvidenceDefinitions) {
        const auto &observed = lineage.at(definition.file);
        if (observed.file != definition.file || observed.payload_sha256.is_zero()) {
            throw std::logic_error{
                "verified P1.8 lineage returned an invalid observed identity"};
        }
        ledger.evidence.push_back({
            std::string{definition.id},
            std::string{kFixtureLocatorPrefix} + std::string{definition.relative_path},
            std::nullopt,
            observed.payload_sha256,
            contract::RightsDisposition::local_evaluation_only,
        });
    }

    ledger.claims.reserve(3);
    ledger.claims.push_back(fixture_claim(ledger.evidence));
    ledger.claims.push_back(
        single_file_claim(kRendererClaimId, kEvidenceDefinitions[4].id));
    ledger.claims.push_back(
        single_file_claim(kConfiguredIrClaimId, kEvidenceDefinitions[5].id));

    ledger.resolutions.reserve(kResolutionDefinitions.size());
    for (const auto &definition : kResolutionDefinitions) {
        ledger.resolutions.push_back({
            std::string{kResolutionIdPrefix} + std::string{definition.parameter_path},
            std::string{definition.parameter_path},
            contract::ResolutionMode::authored,
            std::string{definition.claim_id},
            std::nullopt,
            {},
        });
    }

    ledger.bundle.sha256 = canonical_p18_reference_provenance_digest(ledger);
    const auto report = contract::validate(ledger);
    if (!report.ok()) {
        throw std::logic_error{
            "constructed P1.8 reference provenance ledger is invalid"};
    }
    return P18ReferenceProvenance{std::move(ledger)};
}

} // namespace engine_sim_offline::reference
