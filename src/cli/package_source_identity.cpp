#include "package_source_identity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::cli {
namespace {

inline constexpr std::string_view kDomain =
    "engine-sim-offline/audio-package-source-inputs/1";

struct Record {
    std::string key;
    std::vector<std::byte> bytes;
};

void append_u64(std::vector<std::byte> &output, const std::uint64_t value) {
    for (unsigned index = 0U; index < 8U; ++index) {
        output.push_back(static_cast<std::byte>(value >> (index * 8U)));
    }
}

void append_field(std::vector<std::byte> &output,
                  const std::span<const std::byte> value) {
    append_u64(output, static_cast<std::uint64_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

void append_field(std::vector<std::byte> &output,
                  const std::string_view value) {
    append_field(output, std::as_bytes(std::span{value}));
}

void append_digest(std::vector<std::byte> &output,
                   const contract::Sha256Digest &digest) {
    std::array<std::byte, 32U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(digest.bytes[index]);
    }
    append_field(output, bytes);
}

Record document_record(std::string key, const std::string_view kind,
                       const std::string_view authored_id,
                       const NativeSourceDocument &source) {
    Record result;
    result.key = std::move(key);
    append_field(result.bytes, kind);
    append_field(result.bytes, authored_id);
    append_u64(result.bytes, static_cast<std::uint64_t>(source.bytes.size()));
    append_digest(result.bytes, source.sha256);
    return result;
}

Record asset_record(const OwnedAssetPayload &asset) {
    const auto kind =
        asset.kind == compile::AssetKind::audio
            ? std::string_view{"audio"}
            : std::string_view{"accessory-configuration"};
    Record result;
    result.key = "asset:" + std::string{kind} + ":" + asset.id;
    append_field(result.bytes, "engine-asset");
    append_field(result.bytes, kind);
    append_field(result.bytes, asset.id);
    append_u64(result.bytes, static_cast<std::uint64_t>(asset.bytes.size()));
    append_digest(result.bytes, contract::sha256(asset.bytes));
    return result;
}

} // namespace

contract::ProvenanceBundleRef package_source_inputs_identity(
    const NativeEngineInput &engine, const NativePackageBakeInput &package,
    const std::string_view package_id) {
    std::vector<Record> records;
    records.reserve(2U + engine.assets.size() + package.scenarios.size());
    records.push_back(document_record(
        "document:engine", "engine-document",
        engine.document.engine.identity.id.value, engine.source));
    records.push_back(document_record("document:package-bake",
                                      "package-bake-document",
                                      package.document.id.value,
                                      package.source));
    for (const auto &asset : engine.assets) {
        records.push_back(asset_record(asset));
    }
    for (const auto &scenario : package.scenarios) {
        records.push_back(document_record(
            "scenario:" + scenario.source_id, "scenario-document",
            scenario.source_id + ":" + scenario.document.id.value,
            scenario.source));
    }
    std::ranges::sort(records, {}, &Record::key);

    std::vector<std::byte> closure;
    append_field(closure, kDomain);
    append_u64(closure, static_cast<std::uint64_t>(records.size()));
    for (const auto &record : records) {
        append_field(closure, record.bytes);
    }
    return {std::string{package_id} + "-source-inputs",
            contract::sha256(closure)};
}

} // namespace engine_sim_offline::cli
