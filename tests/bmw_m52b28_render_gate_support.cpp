#include "bmw_m52b28_render_gate_support.hpp"

#include "engine_sim_offline/authoring/parse.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>

namespace engine_sim_offline::test::bmw_m52b28_render_gate {
namespace {

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string diagnostics(
    const authoring::DiagnosticReport &report) {
    std::string result;
    for (const auto &diagnostic : report.diagnostics) {
        if (!result.empty()) {
            result += "; ";
        }
        result += diagnostic.json_pointer + ": " + diagnostic.message;
    }
    return result.empty() ? "no diagnostic detail" : result;
}

template <class Value>
[[nodiscard]] Value require(
    std::variant<Value, authoring::DiagnosticReport> result,
    const std::string_view context) {
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&result)) {
        throw std::runtime_error{std::string{context} + ": " +
                                 diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] std::vector<OwnedAsset> load_assets(
    const authoring::EnginePackageDocument &document,
    const std::filesystem::path &engine_path) {
    const auto base = engine_path.parent_path();
    std::vector<OwnedAsset> result;
    result.reserve(document.presentation.assets.size() +
                   document.engine.accessory_configurations.size());
    for (const auto &asset : document.presentation.assets) {
        result.push_back(
            {compile::AssetKind::audio, asset.id.value,
             read_bytes(base / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        result.push_back(
            {compile::AssetKind::accessory_configuration, asset.id.value,
             read_bytes(base / asset.uri)});
    }
    return result;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

} // namespace

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = digits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[digest.bytes[index] & UINT8_C(0x0f)];
    }
    return result;
}

contract::Sha256Digest digest_from_hex(const std::string_view text) {
    expect(text.size() == 64U, "SHA-256 text has the wrong length");
    const auto nibble = [](const char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw std::runtime_error{"SHA-256 text is not lowercase hexadecimal"};
    };
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2U]) << 4U) |
            nibble(text[index * 2U + 1U]));
    }
    return result;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error{"could not measure " + path.string()};
    }
    stream.seekg(0);
    std::vector<std::byte> result(static_cast<std::size_t>(end));
    stream.read(reinterpret_cast<char *>(result.data()),
                static_cast<std::streamsize>(result.size()));
    if (stream.gcount() != static_cast<std::streamsize>(result.size())) {
        throw std::runtime_error{"incomplete read from " + path.string()};
    }
    return result;
}

compile::CompiledScenario
compile_authored_scenario(const std::filesystem::path &repository_root) {
    const auto engine_path =
        repository_root / "data/engines/bmw-m52b28/engine.json";
    const auto scenario_path =
        repository_root /
        "data/engines/bmw-m52b28/scenarios/"
        "inertial-dyno-1500-6500rpm.json";

    auto engine_document = require(
        authoring::parse_engine_document(read_text(engine_path)),
        "public engine parse failed");
    auto scenario_document = require(
        authoring::parse_scenario_document(read_text(scenario_path)),
        "public scenario parse failed");
    const auto references = authoring::validate_scenario_references(
        scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{"cross-document validation failed: " +
                                 diagnostics(references)};
    }

    const auto assets = load_assets(engine_document, engine_path);
    const auto asset_views = views(assets);
    auto engine = require(
        compile::compile_engine(engine_document, asset_views),
        "public engine compilation failed");
    expect(engine.id() == "bmw-m52b28",
           "public compiled engine retained the wrong authored ID");
    auto scenario = require(
        compile::compile_scenario(engine, scenario_document),
        "public scenario compilation failed");
    expect(scenario.id() ==
               "bmw-m52b28-inertial-dyno-1500-6500rpm",
           "public compiled scenario retained the wrong authored ID");
    return scenario;
}

RenderSinkStatus
VerifyingMemorySink::protocol_error(std::string message) const {
    return RenderSinkError{
        RenderSinkErrorKind::protocol_violation,
        "bmw_authored_json_render_gate_sink_protocol",
        std::move(message),
    };
}

RenderSinkStatus VerifyingMemorySink::begin_transaction(
    const contract::OutputContract &contract_value) {
    ++begin_calls;
    if (state_ != State::idle || begin_calls != 1U) {
        return protocol_error("transaction began more than once");
    }
    output_contract = contract_value;
    state_ = State::begun;
    return std::nullopt;
}

RenderSinkStatus
VerifyingMemorySink::declare_artifact(const PendingArtifact &artifact) {
    ++declaration_calls;
    if (state_ != State::begun || artifact.role.empty() ||
        role_index_.contains(artifact.role)) {
        return protocol_error("artifact declaration was invalid");
    }
    role_index_.emplace(artifact.role, artifacts.size());
    artifacts.push_back({artifact, {}, false, std::nullopt});
    if (artifact.audio.has_value()) {
        const auto bytes_per_frame =
            artifact.audio->sample_encoding_id == "pcm_s24le" ? 3U : 4U;
        artifacts.back().bytes.reserve(
            static_cast<std::size_t>(artifact.audio->frame_count) *
                bytes_per_frame +
            1024U);
    }
    return std::nullopt;
}

RenderSinkStatus
VerifyingMemorySink::write_artifact_chunk(const ArtifactChunk &chunk) {
    ++write_calls;
    const auto found = role_index_.find(std::string{chunk.role});
    if (state_ != State::begun || found == role_index_.end() ||
        chunk.bytes.empty()) {
        return protocol_error("artifact write was undeclared or empty");
    }
    auto &artifact = artifacts[found->second];
    if (artifact.sealed ||
        chunk.byte_offset !=
            static_cast<std::uint64_t>(artifact.bytes.size())) {
        return protocol_error("artifact write was noncontiguous");
    }
    artifact.bytes.insert(artifact.bytes.end(), chunk.bytes.begin(),
                          chunk.bytes.end());
    return std::nullopt;
}

RenderSinkStatus
VerifyingMemorySink::seal_artifact(const contract::ArtifactRecord &record) {
    ++seal_calls;
    const auto found = role_index_.find(record.role);
    if (state_ != State::begun || found == role_index_.end()) {
        return protocol_error("artifact seal was undeclared");
    }
    auto &artifact = artifacts[found->second];
    const auto &pending = artifact.declaration;
    if (artifact.sealed || pending.role != record.role ||
        pending.kind != record.kind ||
        pending.relative_path != record.relative_path ||
        pending.audio != record.audio ||
        pending.diagnostic != record.diagnostic ||
        record.byte_count != artifact.bytes.size() ||
        record.payload_sha256 != contract::sha256(artifact.bytes)) {
        return protocol_error(
            "sealed artifact differed from its declaration or bytes");
    }
    artifact.sealed = true;
    artifact.record = record;
    seals.push_back(record);
    return std::nullopt;
}

RenderSinkStatus
VerifyingMemorySink::commit(const contract::RenderManifest &manifest) {
    ++commit_calls;
    if (state_ != State::begun || commit_calls != 1U ||
        artifacts.size() != seals.size() ||
        !std::ranges::all_of(
            artifacts, [](const auto &value) { return value.sealed; }) ||
        manifest.content.artifacts != seals) {
        return protocol_error("commit did not contain the sealed transaction");
    }
    committed_manifest = manifest;
    state_ = State::committed;
    return std::nullopt;
}

void VerifyingMemorySink::abort() noexcept {
    ++abort_calls;
    state_ = State::aborted;
}

const StoredArtifact &
VerifyingMemorySink::at(const std::string_view role) const {
    const auto found = role_index_.find(std::string{role});
    if (found == role_index_.end()) {
        throw std::out_of_range{"rendered artifact role is unavailable"};
    }
    return artifacts[found->second];
}

} // namespace engine_sim_offline::test::bmw_m52b28_render_gate
