#include "profiles/bmw_m52b28_profile_internal.hpp"

#include "engine_sim_offline/contract/presentation.hpp"

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
constexpr std::string_view kOperatingDigestGrammar =
    "engine-sim-offline.bmw-m52b28-operating-profile-provenance-ledger-digest.v1";
constexpr std::string_view kOperatingSchemaId =
    "engine-sim-offline.bmw-m52b28-operating-profile-provenance.v1";
constexpr std::string_view kOperatingBundleId =
    "bmw-m52b28-low-order-operating-point-v1-provenance";
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

constexpr std::string_view kOperatingCoreClaimId = "bmw-m52b28-operating-core-claim";
constexpr std::string_view kOperatingReferenceFixtureClaimId =
    "bmw-m52b28-operating-reference-fixture-claim";
constexpr std::string_view kOperatingReferenceComponentSeedClaimId =
    "bmw-m52b28-operating-reference-component-seed-claim";
constexpr std::string_view kOperatingLiteratureClaimId =
    "bmw-m52b28-operating-loss-literature-claim";
constexpr std::string_view kOperatingAccessoryClaimId =
    "bmw-m52b28-operating-accessory-configuration-claim";
constexpr std::string_view kOperatingProfileContractClaimId =
    "bmw-m52b28-operating-profile-contract-claim";
constexpr std::string_view kOperatingDeclaredDefaultClaimId =
    "bmw-m52b28-operating-declared-condition-claim";
constexpr std::string_view kOperatingMethodClaimId =
    "bmw-m52b28-operating-implemented-method-claim";
constexpr std::string_view kOperatingDerivedValueClaimId =
    "bmw-m52b28-operating-derived-value-claim";

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

constexpr std::array<EvidenceDefinition, 14> kOperatingEvidenceDefinitions{{
    {
        "operating-point-model-record",
        "docs/model/M4_OPERATING_POINT_MODEL.md",
        kRepositoryContentRevision,
        "254eaf8994ac9884c9ef7e6e14844f2ac751f75d47d227d19a2d587a69f6eb00",
        contract::RightsDisposition::permitted,
    },
    {
        "operating-accessory-configuration",
        "data/profiles/bmw-m52b28/accessory-configurations/"
        "bmw-m52b28-warm-stock-accessories-v1.json",
        kRepositoryContentRevision,
        "ce3cd1bfa0265e5d82e93a70f515cd86d16efa8da4ad5432057372da2b9d8e97",
        contract::RightsDisposition::permitted,
    },
    {
        "admitted-m3-core-model-record",
        "docs/model/M3_PARITY_MODEL.md",
        kRepositoryContentRevision,
        "95be48a8b4e4fd351130994e2245a608ac384da9ae3c9843b7c87075abc26fe9",
        contract::RightsDisposition::permitted,
    },
    {
        "admitted-m3-core-method-configuration",
        "git-blob:760ddd8e436704ed707623a6dad0e6556606d08f",
        "aa1c9a1553b301300258e9fc1de16e6e47c2012c:"
        "docs/model/M3_PARITY_MODEL.md",
        "435441890e0a5f8d01e81995f64f33d4c554144f5b1436895e6816f6db85e34c",
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
        "cycle-quadrature-method-descriptor",
        "compiled-method-descriptor:"
        "four-stroke-piecewise-linear-cycle-quadrature-v1",
        "version 1",
        "57c9b1517deede3285b5c801cb66386a841d0b0dde08bece7eb05fae869a63ac",
        contract::RightsDisposition::permitted,
    },
    {
        "aggregate-loss-method-descriptor",
        "compiled-method-descriptor:chen-flynn-cycle-mean-aggregate-loss-v1",
        "version 1",
        "6fa03e2d9eabfdc7af99dd3e2b2658808dbe388260391780dab4c80bc0c79489",
        contract::RightsDisposition::permitted,
    },
    {
        "generic-four-stroke-friction-prior",
        "https://digital.lib.washington.edu/bitstreams/"
        "6a99eb25-f5a0-43fc-92cc-7fa6ac1c8496/download",
        "Wittenbecher thesis, 2017, pp. 22-23",
        "f378d08af3e9a3b4c7662e2a3f6b2354350317a52edaaa6eb47520f3be24914e",
        contract::RightsDisposition::noassertion,
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

[[nodiscard]] ResolutionSourceDefinition resolution_source(BmwProfileKind profile_kind,
                                                           BmwResolutionSource source) {
    if (profile_kind == BmwProfileKind::parity_request_v1) {
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
            return {contract::ResolutionMode::declared_default,
                    kDeclaredDefaultClaimId};
        case BmwResolutionSource::operating_literature:
        case BmwResolutionSource::accessory_configuration:
        case BmwResolutionSource::implemented_method:
            break;
        }
    } else {
        switch (source) {
        case BmwResolutionSource::legacy_asset:
            return {contract::ResolutionMode::authored, kOperatingCoreClaimId};
        case BmwResolutionSource::reference_fixture:
            return {contract::ResolutionMode::authored,
                    kOperatingReferenceFixtureClaimId};
        case BmwResolutionSource::reference_component_seed:
            return {contract::ResolutionMode::authored,
                    kOperatingReferenceComponentSeedClaimId};
        case BmwResolutionSource::profile_contract:
            return {contract::ResolutionMode::authored,
                    kOperatingProfileContractClaimId};
        case BmwResolutionSource::declared_default:
            return {contract::ResolutionMode::declared_default,
                    kOperatingDeclaredDefaultClaimId};
        case BmwResolutionSource::operating_literature:
            return {contract::ResolutionMode::authored, kOperatingLiteratureClaimId};
        case BmwResolutionSource::accessory_configuration:
            return {contract::ResolutionMode::authored, kOperatingAccessoryClaimId};
        case BmwResolutionSource::implemented_method:
            return {contract::ResolutionMode::authored, kOperatingMethodClaimId};
        case BmwResolutionSource::reference_trajectory:
            break;
        }
    }
    throw std::logic_error{
        "BMW provenance resolution source is unavailable for this profile"};
}

[[nodiscard]] std::string resolution_id(BmwProfileKind profile_kind,
                                        std::uint32_t sequence) {
    const std::string_view prefix = profile_kind == BmwProfileKind::parity_request_v1
                                        ? "bmw-m52b28-m3-resolution-"
                                        : "bmw-m52b28-operating-profile-resolution-";
    return std::string{prefix} + std::to_string(sequence);
}

} // namespace

BmwProvenanceBuilder::BmwProvenanceBuilder(BmwProfileKind profile_kind)
    : profile_kind_(profile_kind) {
    if (profile_kind_ == BmwProfileKind::low_order_operating_point_v1) {
        ledger_.schema_id = kOperatingSchemaId;
        ledger_.bundle.id = kOperatingBundleId;
        ledger_.evidence.reserve(kOperatingEvidenceDefinitions.size());
        for (const auto &definition : kOperatingEvidenceDefinitions) {
            ledger_.evidence.push_back({
                std::string{definition.id},
                std::string{definition.locator},
                std::string{definition.revision},
                digest(definition.sha256),
                definition.rights,
            });
        }

        ledger_.claims.reserve(9);
        ledger_.claims.push_back({
            std::string{kOperatingCoreClaimId},
            contract::ProvenanceOrigin::legacy_asset_unverified,
            {
                citation("legacy-bmw-m52b28-asset",
                         "complete file: BMW M52B28 authored engine values"),
                citation("legacy-engine-sim-objects",
                         "complete file: inherited engine-sim object defaults"),
                citation("legacy-performer-intake",
                         "complete file: performer intake values"),
                citation("admitted-m3-core-model-record",
                         "frozen accepted low-order core values and equations"),
                citation("admitted-m3-core-method-configuration",
                         "complete admitted shared-core method configuration"),
                citation("operating-point-model-record",
                         "section 1: explicitly reused accepted core"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingReferenceFixtureClaimId},
            contract::ProvenanceOrigin::reference_fixture,
            {
                citation("reference-fixture-manifest",
                         "binarySchema.parity, cylinders, and routes"),
                citation("reference-parity-evidence",
                         "complete file: cylinder and route descriptors"),
                citation("admitted-m3-core-model-record",
                         "frozen reference-fixture-resolved core values"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingReferenceComponentSeedClaimId},
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
            std::string{kOperatingLiteratureClaimId},
            contract::ProvenanceOrigin::literature,
            {
                citation("generic-four-stroke-friction-prior",
                         "printed pp. 22-23: generic four-stroke coefficients"),
                citation("operating-point-model-record",
                         "sections 1 and 3: tuple, formula, units, and limitations"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingAccessoryClaimId},
            contract::ProvenanceOrigin::scenario,
            {
                citation("operating-accessory-configuration",
                         "complete content-addressed configuration descriptor"),
                citation("operating-point-model-record",
                         "section 1.1: exact descriptor admission"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingProfileContractClaimId},
            contract::ProvenanceOrigin::scenario,
            {
                citation("operating-point-model-record",
                         "sections 1.1-1.2: exact profile and listening-request "
                         "identity and accounting"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingDeclaredDefaultClaimId},
            contract::ProvenanceOrigin::scenario,
            {
                citation("operating-point-model-record",
                         "sections 1.1 and 4: declared 363.15 K applicability "
                         "condition and caveat"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingMethodClaimId},
            contract::ProvenanceOrigin::scenario,
            {
                citation("cycle-quadrature-method-descriptor",
                         "complete compiled configuration descriptor"),
                citation("aggregate-loss-method-descriptor",
                         "complete compiled configuration descriptor"),
                citation("admitted-m3-core-method-configuration",
                         "complete admitted shared-core method configuration"),
                citation("operating-point-model-record",
                         "sections 1.1-3: admitted method roles"),
            },
            std::nullopt,
        });
        ledger_.claims.push_back({
            std::string{kOperatingDerivedValueClaimId},
            contract::ProvenanceOrigin::derived,
            {
                citation("admitted-m3-core-model-record",
                         "normative shared-core derivation equations"),
                citation("admitted-m3-core-method-configuration",
                         "admitted shared-core derivation configuration"),
                citation("operating-point-model-record",
                         "sections 1.1 and 3: operating-profile derivations"),
            },
            std::nullopt,
        });
        return;
    }
    if (profile_kind_ != BmwProfileKind::parity_request_v1) {
        throw std::logic_error{"unknown BMW profile kind"};
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
    const auto definition = resolution_source(profile_kind_, source);
    auto id = resolution_id(profile_kind_, next_resolution_++);
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

std::string
BmwProvenanceBuilder::add_derived_resolution(std::string parameter_path,
                                             contract::MethodIdentity method,
                                             std::vector<std::string> dependencies) {
    auto id = resolution_id(profile_kind_, next_resolution_++);
    const auto claim_id = profile_kind_ == BmwProfileKind::parity_request_v1
                              ? kDerivedValueClaimId
                              : kOperatingDerivedValueClaimId;
    ledger_.resolutions.push_back({
        id,
        std::move(parameter_path),
        contract::ResolutionMode::derived,
        std::string{claim_id},
        std::move(method),
        std::move(dependencies),
    });
    return id;
}

contract::ProvenanceLedger BmwProvenanceBuilder::finish() {
    const auto grammar = profile_kind_ == BmwProfileKind::parity_request_v1
                             ? kDigestGrammar
                             : kOperatingDigestGrammar;
    ledger_.bundle.sha256 =
        contract::canonical_provenance_ledger_digest(ledger_, grammar);
    return std::move(ledger_);
}

BmwProfileKind BmwProvenanceBuilder::profile_kind() const noexcept {
    return profile_kind_;
}

std::string_view BmwProvenanceBuilder::engine_profile_id() const noexcept {
    return profile_kind_ == BmwProfileKind::parity_request_v1
               ? "bmw-m52b28-legacy-low-order-v1"
               : "bmw-m52b28-low-order-operating-point-v1";
}

std::string_view BmwProvenanceBuilder::provenance_schema_id() const noexcept {
    return profile_kind_ == BmwProfileKind::parity_request_v1 ? kSchemaId
                                                              : kOperatingSchemaId;
}

std::string BmwProvenanceBuilder::profile_path(std::string_view suffix) const {
    const std::string_view root = profile_kind_ == BmwProfileKind::parity_request_v1
                                      ? "engine.physics.legacy-low-order-v1"
                                      : "engine.physics.low-order-operating-point-v1";
    return std::string{root} + "." + std::string{suffix};
}

contract::MethodIdentity legacy_low_order_method() {
    return contract::legacy_low_order_v1_method_identity();
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

contract::Sha256Digest bmw_m52b28_operating_accessory_descriptor_sha256() {
    return digest("ce3cd1bfa0265e5d82e93a70f515cd86d16efa8da4ad5432057372da2b9d8e97");
}

contract::RandomPlanCompilationResult compile_bmw_m52b28_migration_oracle_random_plan(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario) {
    contract::ResolvedRandomnessPolicy policy;
    policy.seed_namespace_id.value = "baked.loaded_acceleration";
    policy.generator.value = contract::pcg32_generator_method_identity();
    policy.derivation.value = contract::component_seed_derivation_method_identity();

    // Direct physics oracles have no presentation consumers. The canonical compiler
    // still derives their combustion lanes from the same policy and public seed used
    // by the complete listening render.
    const contract::PresentationCalibration no_presentation_routes;
    return contract::compile_random_plan(policy, engine, no_presentation_routes,
                                         scenario);
}

} // namespace engine_sim_offline::profiles::detail
