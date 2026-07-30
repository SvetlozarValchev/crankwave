#include "compile/engine_resolver_internal.hpp"

#include "presentation/pcm16_ir_decoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

void add(authoring::DiagnosticReport &report, authoring::DiagnosticCode code,
         std::string path, std::string message) {
    authoring::Diagnostic value;
    value.code = code;
    value.json_pointer = std::move(path);
    value.message = std::move(message);
    report.diagnostics.push_back(std::move(value));
}

[[nodiscard]] std::optional<std::uint8_t> hex_nibble(char byte) noexcept {
    if (byte >= '0' && byte <= '9') {
        return static_cast<std::uint8_t>(byte - '0');
    }
    if (byte >= 'a' && byte <= 'f') {
        return static_cast<std::uint8_t>(byte - 'a' + 10);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<contract::Sha256Digest>
digest_from_hex(std::string_view hex) noexcept {
    if (hex.size() != 64U) {
        return std::nullopt;
    }
    contract::Sha256Digest digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        const auto high = hex_nibble(hex[2U * index]);
        const auto low = hex_nibble(hex[2U * index + 1U]);
        if (!high || !low) {
            return std::nullopt;
        }
        digest.bytes[index] =
            static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return digest;
}

[[nodiscard]] const AssetPayloadView *
find_payload(std::span<const AssetPayloadView> assets, AssetKind kind,
             std::string_view id, std::size_t &matches) {
    const AssetPayloadView *result = nullptr;
    matches = 0;
    for (const auto &asset : assets) {
        if (asset.kind == kind && asset.asset_id == id) {
            result = &asset;
            ++matches;
        }
    }
    return result;
}

[[nodiscard]] std::string evidence_id(const VerifiedEngineAsset &asset) {
    return std::string{"engine.asset."} +
           (asset.kind == AssetKind::audio ? "audio." : "accessory.") +
           asset.asset_id;
}

} // namespace

void verify_engine_assets(const authoring::EnginePackageDocument &document,
                          std::span<const AssetPayloadView> inputs,
                          authoring::DiagnosticReport &report,
                          VerifiedAssets &output) {
    std::set<std::pair<AssetKind, std::string>> expected;
    for (std::size_t index = 0; index < document.presentation.assets.size();
         ++index) {
        const auto &definition = document.presentation.assets[index];
        const auto path = pointer_index("/presentation/assets", index);
        if (definition.kind != authoring::AudioAssetKind::impulse_response) {
            add(report, authoring::DiagnosticCode::unsupported_capability,
                path + "/kind",
                "the current presentation admits impulse-response assets only");
            continue;
        }
        std::size_t matches = 0;
        const auto *payload =
            find_payload(inputs, AssetKind::audio, definition.id.value, matches);
        expected.emplace(AssetKind::audio, definition.id.value);
        if (matches == 0U || payload == nullptr) {
            add(report, authoring::DiagnosticCode::missing_asset, path + "/uri",
                "audio asset bytes were not supplied to the portable compiler");
            continue;
        }
        if (matches != 1U) {
            add(report, authoring::DiagnosticCode::duplicate_id, path + "/id",
                "audio asset payload identity was supplied more than once");
            continue;
        }
        if (payload->bytes.empty()) {
            add(report, authoring::DiagnosticCode::missing_asset, path + "/uri",
                "audio asset payload contains no bytes");
            continue;
        }
        const auto digest = contract::sha256(payload->bytes);
        if (definition.sha256) {
            const auto declared = digest_from_hex(*definition.sha256);
            if (!declared || *declared != digest) {
                add(report, authoring::DiagnosticCode::asset_hash_mismatch,
                    path + "/sha256",
                    "declared audio SHA-256 does not match the supplied bytes");
                continue;
            }
        }
        const auto decoded = presentation::decode_pcm16_ir_wave(payload->bytes);
        const auto *wave = std::get_if<presentation::DecodedPcm16Ir>(&decoded);
        if (wave == nullptr || wave->samples.empty() ||
            wave->meaningful_support_frames == 0U) {
            add(report, authoring::DiagnosticCode::invalid_value, path + "/uri",
                "impulse response must be an admitted nonempty PCM16 mono "
                "44.1-kHz WAVE payload");
            continue;
        }
        output.audio_by_id.emplace(definition.id.value, output.values.size());
        output.audio_media_by_id.emplace(
            definition.id.value,
            contract::AudioMediaContract{
                contract::AudioSampleEncoding::pcm_s16le,
                contract::AudioChannelLayout::mono,
                {presentation::kConfiguredIrSampleRateHz, 1U},
                static_cast<std::uint64_t>(wave->samples.size()),
            });
        output.values.push_back({
            AssetKind::audio,
            definition.id.value,
            definition.uri,
            digest,
            std::vector<std::byte>{payload->bytes.begin(), payload->bytes.end()},
        });
    }

    for (std::size_t index = 0;
         index < document.engine.accessory_configurations.size(); ++index) {
        const auto &definition =
            document.engine.accessory_configurations[index];
        const auto path =
            pointer_index("/engine/accessory_configurations", index);
        std::size_t matches = 0;
        const auto *payload = find_payload(
            inputs, AssetKind::accessory_configuration, definition.id.value,
            matches);
        expected.emplace(AssetKind::accessory_configuration,
                         definition.id.value);
        if (matches == 0U || payload == nullptr) {
            add(report, authoring::DiagnosticCode::missing_asset, path + "/uri",
                "accessory-configuration bytes were not supplied to the portable "
                "compiler");
            continue;
        }
        if (matches != 1U) {
            add(report, authoring::DiagnosticCode::duplicate_id, path + "/id",
                "accessory-configuration payload identity was supplied more than "
                "once");
            continue;
        }
        if (payload->bytes.empty()) {
            add(report, authoring::DiagnosticCode::missing_asset, path + "/uri",
                "accessory-configuration payload contains no bytes");
            continue;
        }
        const auto digest = contract::sha256(payload->bytes);
        if (definition.sha256) {
            const auto declared = digest_from_hex(*definition.sha256);
            if (!declared || *declared != digest) {
                add(report, authoring::DiagnosticCode::asset_hash_mismatch,
                    path + "/sha256",
                    "declared accessory SHA-256 does not match the supplied bytes");
                continue;
            }
        }
        output.accessory_by_id.emplace(definition.id.value,
                                       output.values.size());
        output.values.push_back({
            AssetKind::accessory_configuration,
            definition.id.value,
            definition.uri,
            digest,
            std::vector<std::byte>{payload->bytes.begin(), payload->bytes.end()},
        });
    }

    for (const auto &input : inputs) {
        if (!expected.contains(
                std::pair{input.kind, std::string{input.asset_id}})) {
            add(report, authoring::DiagnosticCode::disconnected_object, "",
                "asset payload '" + std::string{input.asset_id} +
                    "' is not referenced by this engine document");
        }
    }
    if (report.has_errors()) {
        return;
    }

    std::ranges::sort(output.values, [](const auto &left, const auto &right) {
        return std::tie(left.kind, left.asset_id) <
               std::tie(right.kind, right.asset_id);
    });
    output.audio_by_id.clear();
    output.accessory_by_id.clear();
    for (std::size_t index = 0; index < output.values.size(); ++index) {
        const auto &asset = output.values[index];
        if (asset.kind == AssetKind::audio) {
            output.audio_by_id.emplace(asset.asset_id, index);
        } else {
            output.accessory_by_id.emplace(asset.asset_id, index);
        }
    }
}

void attach_asset_evidence(contract::ProvenanceLedger &ledger,
                           const std::vector<VerifiedEngineAsset> &assets) {
    const std::string claim_id = "compiler.engine.authored-product-data";
    auto claim =
        std::ranges::find(ledger.claims, claim_id,
                          &contract::ProvenanceClaim::id);
    if (claim == ledger.claims.end()) {
        return;
    }
    for (const auto &asset : assets) {
        const auto id = evidence_id(asset);
        ledger.evidence.push_back({
            id,
            asset.locator,
            std::nullopt,
            asset.content_sha256,
            contract::RightsDisposition::noassertion,
        });
        claim->citations.push_back({
            id,
            "complete content-addressed asset payload",
        });
    }
    ledger.bundle.sha256 = contract::canonical_provenance_ledger_digest(
        ledger,
        "engine-sim-offline.compiler-resolution-provenance-digest.engine");
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
