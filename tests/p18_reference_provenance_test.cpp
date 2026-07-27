#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_method_identity.hpp"
#include "reference/p18_reference_provenance.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

static_assert(!std::is_default_constructible_v<P18ReferenceProvenance>);
static_assert(
    !std::is_constructible_v<P18ReferenceProvenance, contract::ProvenanceLedger>);
static_assert(std::is_copy_constructible_v<P18ReferenceProvenance>);
static_assert(std::is_move_constructible_v<P18ReferenceProvenance>);
static_assert(!std::is_copy_assignable_v<P18ReferenceProvenance>);
static_assert(!std::is_move_assignable_v<P18ReferenceProvenance>);
static_assert(!std::is_invocable_v<decltype(make_p18_reference_provenance),
                                   const P18ReferenceCatalogV1 &>);

constexpr std::array<std::string_view, kP18ReferenceMethodCount>
    kExpectedMethodConfigurationSha256{
        "3577c0c36a817c0c1c6ba944e0662dbf65cc1c8cedf6a530accff8baf76d7a7e",
        "08cd0ee63b6cc97c097d6a91577d65292a70d4a66302a1d45cdc23c3a47521e3",
        "f508eddbd4e031547bb6e9a4f49f7962c1f7bafdc4d0f740f309e2a370956208",
        "303578ab8b93ec4bde570e555df0034b30461f8680e97d04744bc49d5941f236",
        "f4fd3fd4005e607c79a1f6571166de539c849ab43708899a8eeb7d196ef1c7b3",
        "96e801125575b0a6235671c43a7da437739f969695ebc700a3753b5f5c9387ac",
        "8abbacdc9202a01a05daa306ea42a3fa3dd405a5484000e7e0de5d5673dfc2bc",
        "9758d764d28a60bfcd571fa6af128e06f37c1036b8cc1d65b93c4b2e7cef9554",
        "9879bf8f2186105ec105af5f9b47bd877abb072d598371309c8b025076b28b50",
        "6ab31f524e245a642f6fb9c6fe6dd18b85eaf5c1cd00daf63d1199c18fa188bf",
        "6e332228f8e64f30a821e9961e8a16c8d55da2a0a14a3a4118ee40054b2c4105",
    };

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] contract::Sha256Digest digest(std::string_view value) noexcept {
    return contract::sha256(
        std::as_bytes(std::span<const char>{value.data(), value.size()}));
}

[[nodiscard]] std::string hex(const contract::Sha256Digest &value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(value.bytes.size() * 2, '0');
    for (std::size_t index = 0; index < value.bytes.size(); ++index) {
        result[index * 2] = digits[value.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[value.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] const P18ExpectedSemanticMethod &
expected_method(const P18ReferenceCatalogV1 &catalog, P18ReferenceMethod method) {
    switch (method) {
    case P18ReferenceMethod::audit_reader:
        return catalog.expected_audit_reader;
    case P18ReferenceMethod::excitation_adapter:
        return catalog.expected_excitation_adapter;
    case P18ReferenceMethod::excitation_seam:
        return catalog.expected_excitation_seam;
    case P18ReferenceMethod::random_generator:
        return catalog.expected_random_generator;
    case P18ReferenceMethod::seed_derivation:
        return catalog.expected_seed_derivation;
    case P18ReferenceMethod::reconstruction:
    case P18ReferenceMethod::conditioning:
    case P18ReferenceMethod::impulse_response_conversion:
    case P18ReferenceMethod::convolution:
    case P18ReferenceMethod::publication:
    case P18ReferenceMethod::audition_mix: {
        const auto presentation_index =
            static_cast<std::size_t>(method) -
            static_cast<std::size_t>(P18ReferenceMethod::reconstruction);
        const auto &candidate =
            catalog.expected_presentation.expected_methods[presentation_index];
        expect(static_cast<std::size_t>(candidate.method) == presentation_index,
               "presentation catalog method order changed");
        return candidate.expected_semantic;
    }
    }
    throw std::logic_error{"unknown P1.8 reference method"};
}

void test_method_identities() {
    const auto &catalog = p18_reference_catalog_v1();
    const auto &methods = p18_reference_method_identities_v1();
    expect(methods.size() == kP18ReferenceMethodCount,
           "P1.8 method identity count changed");

    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> hashes;
    for (std::size_t index = 0; index < methods.size(); ++index) {
        const auto method = static_cast<P18ReferenceMethod>(index);
        const auto &actual = methods[index];
        const auto &expected = expected_method(catalog, method);
        expect(actual.method == method, "P1.8 method enum order changed");
        expect(actual.id == expected.expected_id &&
                   actual.version == expected.expected_version,
               "P1.8 method semantic identity differs from the catalog");
        expect(!actual.configuration_descriptor.empty() &&
                   actual.configuration_descriptor.back() == '\n',
               "P1.8 method descriptor is not canonical LF-terminated text");
        expect(actual.configuration_sha256 == digest(actual.configuration_descriptor),
               "P1.8 method configuration digest is not content-derived");
        expect(!actual.configuration_sha256.is_zero(),
               "P1.8 method configuration digest is zero");
        expect(ids.emplace(actual.id).second, "P1.8 method semantic ID is duplicated");
        expect(hashes.emplace(hex(actual.configuration_sha256)).second,
               "P1.8 method configuration digest is duplicated");
        expect(hex(actual.configuration_sha256) ==
                   kExpectedMethodConfigurationSha256[index],
               "P1.8 method configuration digest changed");
        expect(contract::validate(actual.contract_identity()).ok(),
               "P1.8 contract method identity is invalid");
        expect(&p18_reference_method_identity(method) == &actual,
               "P1.8 method lookup did not return the canonical record");
    }
}

constexpr std::array<std::string_view, kP18ReferenceLineageFileCount> kEvidenceIds{
    "p18-fixture-manifest",
    "p18-parity-evidence",
    "p18-audit-input",
    "p18-component-seed-input",
    "p18-presentation-renderer-record",
    "smooth-39-ir",
    "p18-kernel-comparator",
};

constexpr std::array<std::string_view, kP18ReferenceLineageFileCount> kEvidenceLocators{
    "reference/fixtures/bmw-m52b28-p18/manifest.json",
    "reference/fixtures/bmw-m52b28-p18/reference-parity.bin",
    "reference/fixtures/bmw-m52b28-p18/reference-audit.bin",
    "reference/fixtures/bmw-m52b28-p18/component-seeds.bin",
    "reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md",
    "reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav",
    "reference/fixtures/bmw-m52b28-p18/presentation/"
    "smooth_39-192000hz-volume-0p001-f64le.bin",
};

constexpr std::array<std::string_view, 28> kResolutionPaths{
    "presentation.engine_profile_id",
    "presentation.methods.reconstruction",
    "presentation.methods.conditioning",
    "presentation.methods.impulse_response_conversion",
    "presentation.methods.convolution",
    "presentation.methods.publication",
    "presentation.methods.audition_mix",
    "presentation.algorithm_record.semantic_id",
    "presentation.algorithm_record.evidence_source_id",
    "presentation.algorithm_record.content_sha256",
    "presentation.conditioning.jitter_scale",
    "presentation.conditioning.jitter_modulation_cutoff_hz",
    "presentation.conditioning.derivative_mix_01",
    "presentation.conditioning.air_noise_mix_01",
    "presentation.conditioning.air_noise_cutoff_hz",
    "presentation.assets.smooth-39.semantic_id",
    "presentation.assets.smooth-39.evidence_source_id",
    "presentation.assets.smooth-39.content_sha256",
    "presentation.assets.smooth-39.media",
    "presentation.routes.exhaust.reference.0.impulse_response_gain_linear",
    "presentation.routes.exhaust.reference.0.wet_mix_01",
    "presentation.routes.exhaust.reference.1.impulse_response_gain_linear",
    "presentation.routes.exhaust.reference.1.wet_mix_01",
    "presentation.publication.calibration_gain_linear",
    "presentation.audition.selected_routes",
    "presentation.audition.monitoring_gain_linear",
    "presentation.audition.fade_in_duration_s",
    "presentation.audition.fade_out_duration_s",
};

void test_observed_ledger(const P18LoadedReferenceFixture &fixture,
                          const P18ReferenceProvenance &provenance) {
    const auto &ledger = provenance.ledger();
    expect(ledger.schema_id == "engine-sim-offline.p18-reference-provenance.v1",
           "P1.8 provenance schema ID changed");
    expect(ledger.bundle.id == "bmw-m52b28-p18-reference-provenance-v1",
           "P1.8 provenance bundle ID changed");
    expect(ledger.bundle.sha256 == canonical_p18_reference_provenance_digest(ledger),
           "P1.8 provenance bundle is not a canonical self-digest");
    expect(hex(ledger.bundle.sha256) ==
               "76dfb503bc1852f1a1d11f739c612d11e4ad4c05ee0ab5c5eda873a35e04f60d",
           "P1.8 provenance canonical bundle digest changed");
    expect(contract::validate(ledger).ok(), "P1.8 provenance ledger is invalid");

    expect(ledger.evidence.size() == kP18ReferenceLineageFileCount,
           "P1.8 provenance evidence count changed");
    for (std::size_t index = 0; index < ledger.evidence.size(); ++index) {
        const auto file = static_cast<P18ReferenceLineageFile>(index);
        const auto &observed = fixture.verified_lineage.at(file);
        const auto &evidence = ledger.evidence[index];
        expect(evidence.id == kEvidenceIds[index] &&
                   evidence.locator == kEvidenceLocators[index],
               "P1.8 provenance evidence mapping changed");
        expect(!evidence.revision.has_value() &&
                   evidence.content_sha256 == observed.payload_sha256,
               "P1.8 provenance did not use the observed content identity");
        expect(evidence.rights == contract::RightsDisposition::local_evaluation_only,
               "P1.8 provenance widened reference-fixture rights");
    }

    expect(ledger.claims.size() == 3, "P1.8 provenance claim count changed");
    expect(ledger.claims[0].id == "p18-reference-fixture-claim" &&
               ledger.claims[1].id == "p18-presentation-renderer-record-claim" &&
               ledger.claims[2].id == "p18-configured-ir-claim",
           "P1.8 provenance claim identity changed");
    expect(std::ranges::all_of(
               ledger.claims,
               [](const auto &claim) {
                   return claim.origin ==
                              contract::ProvenanceOrigin::reference_fixture &&
                          !claim.uncertainty.has_value();
               }),
           "P1.8 provenance claim semantics changed");
    expect(ledger.claims[0].citations.size() == kP18ReferenceLineageFileCount,
           "P1.8 fixture claim is not backed by all observed lineage");
    for (std::size_t index = 0; index < ledger.claims[0].citations.size(); ++index) {
        expect(ledger.claims[0].citations[index].evidence_id == kEvidenceIds[index] &&
                   ledger.claims[0].citations[index].claim_locator == "complete-file",
               "P1.8 complete-fixture citation mapping changed");
    }
    expect(ledger.claims[1].citations.size() == 1 &&
               ledger.claims[1].citations[0].evidence_id ==
                   "p18-presentation-renderer-record",
           "P1.8 renderer claim cites the wrong evidence");
    expect(ledger.claims[2].citations.size() == 1 &&
               ledger.claims[2].citations[0].evidence_id == "smooth-39-ir",
           "P1.8 IR claim cites the wrong evidence");

    expect(ledger.resolutions.size() == kResolutionPaths.size(),
           "P1.8 provenance resolution count changed");
    std::unordered_set<std::string> resolution_ids;
    for (std::size_t index = 0; index < ledger.resolutions.size(); ++index) {
        const auto &resolution = ledger.resolutions[index];
        const std::string expected_id =
            "p18-reference-resolution/" + std::string{kResolutionPaths[index]};
        expect(resolution.parameter_path == kResolutionPaths[index] &&
                   resolution.id == expected_id,
               "P1.8 provenance resolution mapping changed");
        expect(resolution.mode == contract::ResolutionMode::authored &&
                   !resolution.method.has_value() &&
                   resolution.dependency_parameter_paths.empty(),
               "P1.8 authored reference resolution gained derived semantics");
        const auto expected_claim = index >= 15 && index <= 18
                                        ? "p18-configured-ir-claim"
                                        : "p18-presentation-renderer-record-claim";
        expect(resolution.claim_id == expected_claim,
               "P1.8 presentation leaf cites the wrong claim");
        expect(provenance.resolution_id(kResolutionPaths[index]) == resolution.id,
               "P1.8 provenance resolution lookup failed");
        expect(resolution_ids.emplace(resolution.id).second,
               "P1.8 provenance resolution ID is duplicated");
    }
}

void test_canonical_digest_scope(const P18ReferenceProvenance &provenance) {
    const auto &ledger = provenance.ledger();
    const auto baseline = canonical_p18_reference_provenance_digest(ledger);

    auto changed_self_digest = ledger;
    changed_self_digest.bundle.sha256.bytes[0] ^= 0xffU;
    expect(canonical_p18_reference_provenance_digest(changed_self_digest) == baseline,
           "P1.8 provenance digest recursively includes itself");

    auto changed_evidence = ledger;
    changed_evidence.evidence[0].content_sha256->bytes[0] ^= 0xffU;
    expect(canonical_p18_reference_provenance_digest(changed_evidence) != baseline,
           "P1.8 provenance digest omits evidence content identity");

    auto changed_locator = ledger;
    changed_locator.evidence[0].locator += "-changed";
    expect(canonical_p18_reference_provenance_digest(changed_locator) != baseline,
           "P1.8 provenance digest omits evidence locator");

    auto changed_revision = ledger;
    changed_revision.evidence[0].revision = "revision";
    expect(canonical_p18_reference_provenance_digest(changed_revision) != baseline,
           "P1.8 provenance digest omits evidence revision");

    auto changed_claim = ledger;
    changed_claim.claims[0].citations[0].claim_locator += "-changed";
    expect(canonical_p18_reference_provenance_digest(changed_claim) != baseline,
           "P1.8 provenance digest omits claim content");

    auto changed_resolution = ledger;
    changed_resolution.resolutions[0].parameter_path += "-changed";
    expect(canonical_p18_reference_provenance_digest(changed_resolution) != baseline,
           "P1.8 provenance digest omits resolution content");

    auto changed_method = ledger;
    changed_method.resolutions[0].method =
        p18_reference_method_identity(P18ReferenceMethod::reconstruction)
            .contract_identity();
    expect(canonical_p18_reference_provenance_digest(changed_method) != baseline,
           "P1.8 provenance digest omits optional method content");

    auto changed_dependencies = ledger;
    changed_dependencies.resolutions[0].dependency_parameter_paths.push_back(
        ledger.resolutions[1].parameter_path);
    expect(canonical_p18_reference_provenance_digest(changed_dependencies) != baseline,
           "P1.8 provenance digest omits resolution dependencies");

    auto negative_zero = ledger;
    negative_zero.claims[0].uncertainty =
        contract::UncertaintyStatement{-0.0, "signed-zero-test"};
    auto positive_zero = negative_zero;
    positive_zero.claims[0].uncertainty->standard_uncertainty = 0.0;
    expect(canonical_p18_reference_provenance_digest(negative_zero) ==
               canonical_p18_reference_provenance_digest(positive_zero),
           "P1.8 provenance digest did not canonicalize signed zero");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "expected the P1.8 fixture root argument");
        test_method_identities();
        const auto fixture = load_p18_reference_fixture(std::filesystem::path{argv[1]});
        const auto provenance = make_p18_reference_provenance(fixture.verified_lineage);
        test_observed_ledger(fixture, provenance);
        test_canonical_digest_scope(provenance);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
