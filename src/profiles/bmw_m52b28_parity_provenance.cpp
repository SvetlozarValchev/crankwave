#include "profiles/bmw_m52b28_parity_request_internal.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::profiles::detail {
namespace {

constexpr std::string_view kDigestGrammar =
    "engine-sim-offline.m3-bmw-provenance-ledger-digest.v1";
constexpr std::string_view kSchemaId = "engine-sim-offline.m3-bmw-provenance.v1";
constexpr std::string_view kBundleId = "bmw-m52b28-m3-request-provenance-v1";
constexpr std::string_view kRepositoryContentRevision = "repository content";
constexpr std::string_view kLegacyRevision = "9617562a7a5615c2bf84c9ec39cd5ae25c560059";
constexpr std::string_view kModelRecordSha256 =
    "435441890e0a5f8d01e81995f64f33d4c554144f5b1436895e6816f6db85e34c";
constexpr std::string_view kRequestRecordSha256 =
    "c64ab8b9c2f8c78a151222d889865269be19cc521e6852c46ddf3450869e75e4";

constexpr std::string_view kLegacyAssetClaimId = "bmw-m52b28-legacy-asset-claim";
constexpr std::string_view kReferenceFixtureClaimId =
    "bmw-m52b28-reference-fixture-claim";
constexpr std::string_view kReferenceTrajectoryClaimId =
    "bmw-m52b28-reference-trajectory-claim";
constexpr std::string_view kReferenceComponentSeedClaimId =
    "bmw-m52b28-reference-component-seed-claim";
constexpr std::string_view kScenarioClaimId = "bmw-m52b28-scenario-claim";
constexpr std::string_view kDeclaredDefaultClaimId =
    "bmw-m52b28-declared-default-claim";
constexpr std::string_view kDerivedValueClaimId = "bmw-m52b28-derived-value-claim";

struct EvidenceDefinition {
    std::string_view id;
    std::string_view locator;
    std::string_view revision;
    std::string_view sha256;
    contract::RightsDisposition rights;
};

constexpr std::array<EvidenceDefinition, 9> kEvidenceDefinitions{{
    {
        "m3-parity-model-record",
        "docs/model/M3_PARITY_MODEL.md",
        kRepositoryContentRevision,
        kModelRecordSha256,
        contract::RightsDisposition::permitted,
    },
    {
        "m3-bmw-request-record",
        "docs/contracts/M3_BMW_REQUEST.md",
        kRepositoryContentRevision,
        kRequestRecordSha256,
        contract::RightsDisposition::permitted,
    },
    {
        "legacy-bmw-m52b28-asset",
        "assets/engines/bmw/M52B28.mr",
        kLegacyRevision,
        "2c7746f82e86cc22b0ab243f61e7fb8c155c3abf1084ad7b6f8bfee3d4e875a9",
        contract::RightsDisposition::noassertion,
    },
    {
        "legacy-engine-sim-objects",
        "es/objects/objects.mr",
        kLegacyRevision,
        "f8214983c816f0a2e8d2cf1d733d10ea7adfbdaef94965b9c969c49e35e14dba",
        contract::RightsDisposition::noassertion,
    },
    {
        "legacy-performer-intake",
        "es/part-library/parts/intakes.mr",
        kLegacyRevision,
        "f2331225d54ec56da44b5b658cfd51b859eb77c9bc5700a8dcace7513f5f8b1e",
        contract::RightsDisposition::noassertion,
    },
    {
        "reference-fixture-manifest",
        "reference/fixtures/bmw-m52b28-p18/manifest.json",
        kRepositoryContentRevision,
        "52d694ba6edc8771b5a4c394d5b62573c22b38e8ba4ef7e2f5bc8c8fb6decc07",
        contract::RightsDisposition::local_evaluation_only,
    },
    {
        "reference-parity-evidence",
        "reference/fixtures/bmw-m52b28-p18/reference-parity.bin",
        kRepositoryContentRevision,
        "19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c",
        contract::RightsDisposition::local_evaluation_only,
    },
    {
        "reference-component-seed-evidence",
        "reference/fixtures/bmw-m52b28-p18/component-seeds.bin",
        kRepositoryContentRevision,
        "ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f",
        contract::RightsDisposition::local_evaluation_only,
    },
    {
        "engine-sim-mit-notice",
        "reference/oracles/bmw-m52b28/engine-sim-MIT.txt",
        kRepositoryContentRevision,
        "9f64449d4ef2db6b57d6af9d36e5eca5b6de3ece2db14a847ae71d4e0dcbff15",
        contract::RightsDisposition::permitted,
    },
}};

struct ResolutionSourceDefinition {
    contract::ResolutionMode mode;
    std::string_view claim_id;
};

[[nodiscard]] std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw std::logic_error{"invalid embedded BMW provenance digest"};
}

[[nodiscard]] contract::Sha256Digest digest(std::string_view hex) {
    if (hex.size() != 64) {
        throw std::logic_error{"invalid embedded BMW provenance digest length"};
    }
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (hex_nibble(hex[index * 2]) << 4U) | hex_nibble(hex[index * 2 + 1]));
    }
    return result;
}

[[nodiscard]] contract::EvidenceCitation citation(std::string_view evidence_id,
                                                  std::string_view claim_locator) {
    return {std::string{evidence_id}, std::string{claim_locator}};
}

[[nodiscard]] ResolutionSourceDefinition resolution_source(BmwResolutionSource source) {
    switch (source) {
    case BmwResolutionSource::legacy_asset:
        return {contract::ResolutionMode::authored, kLegacyAssetClaimId};
    case BmwResolutionSource::reference_fixture:
        return {contract::ResolutionMode::authored, kReferenceFixtureClaimId};
    case BmwResolutionSource::reference_trajectory:
        return {contract::ResolutionMode::authored, kReferenceTrajectoryClaimId};
    case BmwResolutionSource::reference_component_seed:
        return {contract::ResolutionMode::authored, kReferenceComponentSeedClaimId};
    case BmwResolutionSource::profile_contract:
        return {contract::ResolutionMode::authored, kScenarioClaimId};
    case BmwResolutionSource::declared_default:
        return {contract::ResolutionMode::declared_default, kDeclaredDefaultClaimId};
    case BmwResolutionSource::operating_literature:
    case BmwResolutionSource::accessory_configuration:
    case BmwResolutionSource::implemented_method:
        break;
    }
    throw std::logic_error{"unknown BMW provenance resolution source"};
}

[[nodiscard]] std::string resolution_id(std::uint32_t sequence) {
    return "bmw-m52b28-m3-resolution-" + std::to_string(sequence);
}

} // namespace

BmwProvenanceBuilder::BmwProvenanceBuilder(BmwProfileKind profile_kind)
    : profile_kind_(profile_kind) {
    if (profile_kind_ != BmwProfileKind::parity_request_v1) {
        throw std::logic_error{"BMW operating-profile provenance is not configured"};
    }
    ledger_.schema_id = kSchemaId;
    ledger_.bundle.id = kBundleId;

    ledger_.evidence.reserve(kEvidenceDefinitions.size());
    for (const auto &definition : kEvidenceDefinitions) {
        ledger_.evidence.push_back({
            std::string{definition.id},
            std::string{definition.locator},
            std::string{definition.revision},
            digest(definition.sha256),
            definition.rights,
        });
    }

    ledger_.claims.reserve(7);
    ledger_.claims.push_back({
        std::string{kLegacyAssetClaimId},
        contract::ProvenanceOrigin::legacy_asset_unverified,
        {
            citation("legacy-bmw-m52b28-asset",
                     "complete file: BMW M52B28 authored engine values"),
            citation("legacy-engine-sim-objects",
                     "complete file: inherited engine-sim object defaults"),
            citation("legacy-performer-intake",
                     "complete file: performer intake values"),
            citation("m3-parity-model-record",
                     "sections 2-4: frozen effective legacy values"),
        },
        std::nullopt,
    });
    ledger_.claims.push_back({
        std::string{kReferenceFixtureClaimId},
        contract::ProvenanceOrigin::reference_fixture,
        {
            citation("reference-fixture-manifest",
                     "binarySchema.parity, cylinders, and routes"),
            citation("reference-parity-evidence",
                     "complete file: ESOPAR01 header and cylinder descriptors"),
            citation("m3-parity-model-record",
                     "section 4: frozen reference-fixture-resolved values"),
        },
        std::nullopt,
    });
    ledger_.claims.push_back({
        std::string{kReferenceTrajectoryClaimId},
        contract::ProvenanceOrigin::reference_fixture,
        {
            citation("reference-fixture-manifest",
                     "files.reference-parity.bin and capture"),
            citation("reference-parity-evidence",
                     "complete file: ESOPAR01 engine-speed-RPM lane"),
        },
        std::nullopt,
    });
    ledger_.claims.push_back({
        std::string{kReferenceComponentSeedClaimId},
        contract::ProvenanceOrigin::reference_fixture,
        {
            citation("reference-fixture-manifest",
                     "determinism.componentSeedsHex.combustion"),
            citation("reference-component-seed-evidence",
                     "complete file: combustion PCG32 pairs"),
        },
        std::nullopt,
    });
    ledger_.claims.push_back({
        std::string{kScenarioClaimId},
        contract::ProvenanceOrigin::scenario,
        {
            citation("m3-bmw-request-record",
                     "sections 2, 4, and 5: request and scenario choices"),
        },
        std::nullopt,
    });
    ledger_.claims.push_back({
        std::string{kDeclaredDefaultClaimId},
        contract::ProvenanceOrigin::scenario,
        {
            citation("m3-bmw-request-record",
                     "section 6: declared ambient and thermal defaults"),
        },
        std::nullopt,
    });
    ledger_.claims.push_back({
        std::string{kDerivedValueClaimId},
        contract::ProvenanceOrigin::derived,
        {
            citation("m3-bmw-request-record",
                     "sections 3 and 6-7: resolved calculations"),
            citation("m3-parity-model-record",
                     "normative legacy_low_order_v1 derivation equations"),
        },
        std::nullopt,
    });
}

std::string BmwProvenanceBuilder::add_resolution(std::string parameter_path,
                                                 BmwResolutionSource source) {
    const auto definition = resolution_source(source);
    auto id = resolution_id(next_resolution_++);
    ledger_.resolutions.push_back({
        id,
        std::move(parameter_path),
        definition.mode,
        std::string{definition.claim_id},
        std::nullopt,
        {},
    });
    return id;
}

std::string BmwProvenanceBuilder::add_derived_resolution(
    std::string parameter_path, contract::MethodIdentity method,
    std::vector<std::string> dependencies) {
    auto id = resolution_id(next_resolution_++);
    ledger_.resolutions.push_back({
        id,
        std::move(parameter_path),
        contract::ResolutionMode::derived,
        std::string{kDerivedValueClaimId},
        std::move(method),
        std::move(dependencies),
    });
    return id;
}

contract::ProvenanceLedger BmwProvenanceBuilder::finish() {
    ledger_.bundle.sha256 =
        contract::canonical_provenance_ledger_digest(ledger_, kDigestGrammar);
    return std::move(ledger_);
}

BmwProfileKind BmwProvenanceBuilder::profile_kind() const noexcept {
    return profile_kind_;
}

std::string
BmwProvenanceBuilder::profile_path(std::string_view suffix) const {
    constexpr std::string_view kParityRoot =
        "engine.physics.legacy-low-order-v1";
    return std::string{kParityRoot} + "." + std::string{suffix};
}

contract::MethodIdentity legacy_low_order_method() {
    return {
        "legacy_low_order_v1",
        1,
        digest(kModelRecordSha256),
    };
}

contract::MethodIdentity fixed_rate_rpm_method() {
    return {
        "fixed-rate-post-step-rpm-binary64-v1",
        1,
        digest(kRequestRecordSha256),
    };
}

contract::MethodIdentity derived_method(std::string id) {
    return {
        std::move(id),
        1,
        digest(kModelRecordSha256),
    };
}

} // namespace engine_sim_offline::profiles::detail
