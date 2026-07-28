#include "engine_sim_offline/profiles/bmw_m52b28_parity_request.hpp"
#include "engine_sim_offline/render.hpp"

#include "presentation/presentation_method_registry.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/reference_parity_v1_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

constexpr std::string_view kCombinedProvenanceDigestGrammar =
    "engine-sim-offline.bmw-public-render-provenance-ledger-digest.v1";

struct PublicAudioGolden {
    P18ReferenceAudioArtifact artifact = P18ReferenceAudioArtifact::exhaust_0_dry;
    std::uint64_t byte_count = 0;
    std::string_view container_sha256;
};

constexpr std::array<PublicAudioGolden, kP18ReferenceAudioArtifactCount>
    kPublicAudioGoldens{{
        {
            P18ReferenceAudioArtifact::exhaust_0_dry,
            UINT64_C(11520058),
            "d02195293a3b8d29c17645af6601feec4f740c306306d1a241796c28fcac3796",
        },
        {
            P18ReferenceAudioArtifact::exhaust_0_configured_ir,
            UINT64_C(11520058),
            "33e0aa8dda0b75c7e18f3db840013aee20db98609e79ab9f0aa14dab26b6f094",
        },
        {
            P18ReferenceAudioArtifact::exhaust_0_selected,
            UINT64_C(11520058),
            "33e0aa8dda0b75c7e18f3db840013aee20db98609e79ab9f0aa14dab26b6f094",
        },
        {
            P18ReferenceAudioArtifact::exhaust_1_dry,
            UINT64_C(11520058),
            "5f5c3e77dbf50417a037968d034b04ca800efc47d80e3ecc95820bb4d8707c29",
        },
        {
            P18ReferenceAudioArtifact::exhaust_1_configured_ir,
            UINT64_C(11520058),
            "78cb12cd067153a18ac3d666f62db4e50a62b88bf73578779ccd36c714e3b28b",
        },
        {
            P18ReferenceAudioArtifact::exhaust_1_selected,
            UINT64_C(11520058),
            "78cb12cd067153a18ac3d666f62db4e50a62b88bf73578779ccd36c714e3b28b",
        },
        {
            P18ReferenceAudioArtifact::master_raw,
            UINT64_C(11520058),
            "bcc0278990e0797217a739af9e6358113ee5c71cbdb144a05bab29e012e5b55d",
        },
        {
            P18ReferenceAudioArtifact::master_audition,
            UINT64_C(8640526),
            "919c2fa32a8c082a9c165106c57b4161393562f552d0569d1c9d3c098612348a",
        },
    }};
constexpr std::string_view kExpectedPublicAuditionPcm24Sha256 =
    "b7dfc8d3cb7bbc473a83192904dd8fb01356494cf4177e532c42fc7afc148d1d";

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string report_text(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        if (!result.empty()) {
            result += "; ";
        }
        result += issue.path + ": " + issue.message;
    }
    return result;
}

void expect_valid(const contract::ValidationReport &report, std::string_view context) {
    if (!report.ok()) {
        throw std::runtime_error{std::string{context} + ": " + report_text(report)};
    }
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path,
                                                std::uint64_t expected_byte_count) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    expect(input.is_open(), "could not open a BMW integration input");
    const auto end = input.tellg();
    expect(end >= 0, "could not measure a BMW integration input");
    expect(static_cast<std::uint64_t>(end) == expected_byte_count,
           "BMW integration input byte count changed");
    input.seekg(0);

    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
           "BMW integration input read was incomplete");
    return bytes;
}

[[nodiscard]] std::vector<double>
decode_rpm_lane(std::span<const std::byte> parity_bytes) {
    auto decoded = decode_reference_parity_v1(parity_bytes);
    const auto *parity = std::get_if<DecodedReferenceParityV1>(&decoded);
    expect(parity != nullptr, "verified BMW parity input failed strict decoding");
    expect(parity->frames.size() == kReferenceParityV1RecordCount,
           "BMW parity input has the wrong RPM-lane extent");

    std::vector<double> rpm;
    rpm.reserve(parity->frames.size());
    for (const auto &frame : parity->frames) {
        rpm.push_back(frame.engine_speed_rpm);
    }
    return rpm;
}

[[nodiscard]] profiles::BmwM52b28ParityRequest
make_public_request(const std::filesystem::path &fixture_root,
                    const P18ReferenceCatalogV1 &catalog) {
    const auto &expected = catalog.expected_lineage_files[static_cast<std::size_t>(
        P18ReferenceLineageFile::parity_evidence)];
    expect(expected.file == P18ReferenceLineageFile::parity_evidence,
           "P1.8 lineage catalog order changed");
    auto bytes = read_bytes(fixture_root / expected.expected_relative_path,
                            expected.expected_byte_count);
    expect(contract::sha256(bytes) == expected.expected_sha256,
           "BMW parity input differs from the immutable catalog");

    auto result = profiles::make_bmw_m52b28_parity_request(decode_rpm_lane(bytes));
    auto *request = std::get_if<profiles::BmwM52b28ParityRequest>(&result);
    if (request == nullptr) {
        throw std::runtime_error{
            "exact BMW RPM lane did not construct the public parity request: " +
            report_text(std::get<contract::ValidationReport>(result))};
    }
    expect_valid(profiles::validate_bmw_m52b28_parity_request(*request),
                 "public BMW request exact validation failed");
    return std::move(*request);
}

[[nodiscard]] double binary64(P18ExpectedBinary64 expected) noexcept {
    return std::bit_cast<double>(expected.expected_ieee754_bits);
}

class CombinedProvenanceBuilder {
  public:
    CombinedProvenanceBuilder(contract::ProvenanceLedger public_request_provenance,
                              const P18ReferenceCatalogV1 &catalog,
                              const contract::Sha256Digest &configured_ir_sha256)
        : provenance_(std::move(public_request_provenance)) {
        provenance_.bundle.id = "bmw-m52b28-public-render-provenance-v1";
        provenance_.evidence.push_back({
            "p18-presentation-renderer-record",
            "reference/fixtures/bmw-m52b28-p18/P18_PRESENTATION_RENDERER.md",
            std::string{"repository content"},
            lineage_digest(catalog, P18ReferenceLineageFile::renderer_algorithm_record),
            contract::RightsDisposition::local_evaluation_only,
        });
        provenance_.evidence.push_back({
            "smooth-39-ir",
            "reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav",
            std::string{"repository content"},
            configured_ir_sha256,
            contract::RightsDisposition::local_evaluation_only,
        });
        provenance_.claims.push_back({
            "bmw-m52b28-public-render-presentation-claim",
            contract::ProvenanceOrigin::reference_fixture,
            {
                {
                    "p18-presentation-renderer-record",
                    "complete file: frozen presentation calibration and methods",
                },
                {
                    "smooth-39-ir",
                    "complete file: configured static impulse response",
                },
            },
            std::nullopt,
        });
    }

    template <class Value>
    [[nodiscard]] contract::ResolvedValue<Value> resolved(Value value,
                                                          std::string parameter_path) {
        auto id =
            "bmw-m52b28-public-render-resolution-" + std::to_string(next_resolution_++);
        provenance_.resolutions.push_back({
            id,
            std::move(parameter_path),
            contract::ResolutionMode::authored,
            "bmw-m52b28-public-render-presentation-claim",
            std::nullopt,
            {},
        });
        return {std::move(value), std::move(id)};
    }

    [[nodiscard]] contract::ProvenanceLedger finish() && {
        provenance_.bundle.sha256 = contract::canonical_provenance_ledger_digest(
            provenance_, kCombinedProvenanceDigestGrammar);
        return std::move(provenance_);
    }

  private:
    [[nodiscard]] static contract::Sha256Digest
    lineage_digest(const P18ReferenceCatalogV1 &catalog, P18ReferenceLineageFile file) {
        const auto found = std::ranges::find(catalog.expected_lineage_files, file,
                                             &P18ExpectedLineageFile::file);
        if (found == catalog.expected_lineage_files.end()) {
            throw std::logic_error{"P1.8 lineage catalog is incomplete"};
        }
        return found->expected_sha256;
    }

    contract::ProvenanceLedger provenance_;
    std::uint32_t next_resolution_ = 1;
};

[[nodiscard]] const P18ExpectedRoute &
expected_route(const P18ReferenceCatalogV1 &catalog, P18ReferenceRoute route) {
    const auto found =
        std::ranges::find(catalog.expected_routes, route, &P18ExpectedRoute::route);
    if (found == catalog.expected_routes.end()) {
        throw std::logic_error{"P1.8 route catalog is incomplete"};
    }
    return *found;
}

[[nodiscard]] contract::PresentationCalibration
make_presentation(CombinedProvenanceBuilder &builder,
                  const profiles::BmwM52b28ParityRequest &request,
                  const P18ReferenceCatalogV1 &catalog,
                  const contract::Sha256Digest &configured_ir_sha256) {
    const auto &policy = catalog.expected_presentation;
    const auto &media = policy.expected_configured_ir_media;
    const auto &scalars = policy.expected_scalars;
    const auto &methods = presentation::implemented_presentation_method_identities();

    contract::PresentationCalibration result;
    result.schema_version = 2;
    result.calibration_id = std::string{catalog.expected_presentation_calibration_id};
    result.engine_profile_id = builder.resolved(request.engine.profile_id.value,
                                                "presentation.engine_profile_id");
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
    result.provenance_schema_id = request.provenance.schema_id;
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

[[nodiscard]] RenderSpecification
make_specification(const profiles::BmwM52b28ParityRequest &request,
                   const P18ReferenceCatalogV1 &catalog,
                   std::vector<std::byte> configured_ir_bytes) {
    const auto configured_ir_sha256 = contract::sha256(configured_ir_bytes);
    CombinedProvenanceBuilder builder{
        request.provenance,
        catalog,
        configured_ir_sha256,
    };
    auto presentation =
        make_presentation(builder, request, catalog, configured_ir_sha256);
    auto randomness = make_randomness(builder);
    return {
        request.engine,
        std::move(presentation),
        std::move(randomness),
        std::move(builder).finish(),
        contract::bmw_m52b28_reference_source_matrix_v1(),
        {
            {
                catalog.expected_presentation.expected_configured_ir_media
                    .expected_asset_id,
                std::move(configured_ir_bytes),
            },
        },
    };
}

[[nodiscard]] RenderSinkStatus sink_protocol_error(std::string message) {
    return RenderSinkError{
        RenderSinkErrorKind::protocol_violation,
        "bmw_public_success_sink_protocol",
        std::move(message),
    };
}

struct StoredArtifact {
    PendingArtifact declaration;
    std::vector<std::byte> bytes;
    bool sealed = false;
    std::optional<contract::ArtifactRecord> record;
};

class VerifyingMemorySink final : public RenderSink {
  public:
    std::size_t begin_calls = 0;
    std::size_t declaration_calls = 0;
    std::size_t write_calls = 0;
    std::size_t seal_calls = 0;
    std::size_t commit_calls = 0;
    std::size_t abort_calls = 0;
    std::optional<contract::OutputContract> output_contract;
    std::vector<StoredArtifact> artifacts;
    std::vector<contract::ArtifactRecord> seals;
    std::optional<contract::RenderManifest> committed_manifest;

    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &contract) override {
        ++begin_calls;
        if (begin_calls != 1 || state_ != State::idle) {
            return sink_protocol_error("transaction began more than once");
        }
        output_contract = contract;
        state_ = State::begun;
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override {
        ++declaration_calls;
        if (state_ != State::begun || artifact.role.empty() ||
            role_index_.contains(artifact.role)) {
            return sink_protocol_error("artifact declaration was invalid");
        }
        const auto index = artifacts.size();
        role_index_.emplace(artifact.role, index);
        artifacts.push_back({artifact, {}, false, std::nullopt});
        if (artifact.audio.has_value()) {
            const auto sample_bytes =
                artifact.audio->sample_encoding_id == "pcm_s24le" ? 3U : 4U;
            artifacts.back().bytes.reserve(
                static_cast<std::size_t>(artifact.audio->frame_count) * sample_bytes +
                1024U);
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override {
        ++write_calls;
        const auto found = role_index_.find(std::string{chunk.role});
        if (state_ != State::begun || found == role_index_.end() ||
            chunk.bytes.empty()) {
            return sink_protocol_error("artifact write was undeclared or empty");
        }
        auto &artifact = artifacts[found->second];
        if (artifact.sealed ||
            chunk.byte_offset != static_cast<std::uint64_t>(artifact.bytes.size())) {
            return sink_protocol_error("artifact write was noncontiguous");
        }
        artifact.bytes.insert(artifact.bytes.end(), chunk.bytes.begin(),
                              chunk.bytes.end());
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override {
        ++seal_calls;
        const auto found = role_index_.find(record.role);
        if (state_ != State::begun || found == role_index_.end()) {
            return sink_protocol_error("artifact seal was undeclared");
        }
        auto &artifact = artifacts[found->second];
        const auto &pending = artifact.declaration;
        if (artifact.sealed || pending.role != record.role ||
            pending.kind != record.kind ||
            pending.relative_path != record.relative_path ||
            pending.audio != record.audio || pending.diagnostic != record.diagnostic ||
            record.byte_count != artifact.bytes.size() ||
            record.payload_sha256 != contract::sha256(artifact.bytes)) {
            return sink_protocol_error(
                "sealed artifact differed from its declaration or streamed bytes");
        }
        artifact.sealed = true;
        artifact.record = record;
        seals.push_back(record);
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    commit(const contract::RenderManifest &manifest) override {
        ++commit_calls;
        if (state_ != State::begun || commit_calls != 1 ||
            artifacts.size() != seals.size() ||
            !std::ranges::all_of(
                artifacts, [](const auto &artifact) { return artifact.sealed; }) ||
            manifest.content.artifacts != seals) {
            return sink_protocol_error(
                "commit did not contain the exact sealed transaction");
        }
        committed_manifest = manifest;
        state_ = State::committed;
        return std::nullopt;
    }

    void abort() noexcept override {
        ++abort_calls;
        state_ = State::aborted;
    }

    [[nodiscard]] const StoredArtifact &at(std::string_view role) const {
        const auto found = role_index_.find(std::string{role});
        if (found == role_index_.end()) {
            throw std::out_of_range{"rendered artifact role is unavailable"};
        }
        return artifacts[found->second];
    }

  private:
    enum class State : std::uint8_t {
        idle,
        begun,
        committed,
        aborted,
    };

    State state_ = State::idle;
    std::unordered_map<std::string, std::size_t> role_index_;
};

[[nodiscard]] std::uint32_t read_u32le(std::span<const std::byte> bytes,
                                       std::size_t offset) {
    expect(offset <= bytes.size() && bytes.size() - offset >= 4,
           "WAVE u32 read was out of range");
    std::uint32_t result = 0;
    for (unsigned index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << (index * 8U);
    }
    return result;
}

[[nodiscard]] bool fourcc_is(std::span<const std::byte> bytes, std::size_t offset,
                             std::string_view expected) {
    if (expected.size() != 4 || offset > bytes.size() || bytes.size() - offset < 4) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (std::to_integer<unsigned char>(bytes[offset + index]) !=
            static_cast<unsigned char>(expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string fourcc(std::span<const std::byte> bytes, std::size_t offset) {
    expect(offset <= bytes.size() && bytes.size() - offset >= 4,
           "WAVE FourCC read was out of range");
    std::string result(4, '\0');
    for (std::size_t index = 0; index < 4; ++index) {
        result[index] =
            static_cast<char>(std::to_integer<unsigned char>(bytes[offset + index]));
    }
    return result;
}

struct ParsedWave {
    std::span<const std::byte> data;
    std::unordered_map<std::string, std::string> info;
};

[[nodiscard]] ParsedWave parse_wave(std::span<const std::byte> bytes) {
    expect(bytes.size() >= 12 && fourcc_is(bytes, 0, "RIFF") &&
               fourcc_is(bytes, 8, "WAVE"),
           "artifact is not a RIFF/WAVE container");
    expect(static_cast<std::uint64_t>(read_u32le(bytes, 4)) + 8U == bytes.size(),
           "RIFF size does not describe the complete WAVE container");

    ParsedWave result;
    for (std::size_t offset = 12; offset < bytes.size();) {
        expect(bytes.size() - offset >= 8, "truncated top-level WAVE chunk");
        const auto id = fourcc(bytes, offset);
        const auto payload_size =
            static_cast<std::size_t>(read_u32le(bytes, offset + 4));
        const auto payload_offset = offset + 8;
        expect(payload_offset <= bytes.size() &&
                   payload_size <= bytes.size() - payload_offset,
               "top-level WAVE chunk exceeds the container");

        if (id == "data") {
            expect(result.data.empty(), "WAVE contains duplicate data chunks");
            result.data = bytes.subspan(payload_offset, payload_size);
        } else if (id == "LIST" && payload_size >= 4 &&
                   fourcc_is(bytes, payload_offset, "INFO")) {
            const auto list_end = payload_offset + payload_size;
            for (std::size_t info_offset = payload_offset + 4;
                 info_offset < list_end;) {
                expect(list_end - info_offset >= 8, "truncated INFO subchunk");
                const auto info_id = fourcc(bytes, info_offset);
                const auto info_size =
                    static_cast<std::size_t>(read_u32le(bytes, info_offset + 4));
                const auto text_offset = info_offset + 8;
                expect(text_offset <= list_end && info_size <= list_end - text_offset &&
                           info_size > 0,
                       "INFO subchunk exceeds its LIST");
                expect(bytes[text_offset + info_size - 1] == std::byte{0},
                       "INFO text is not NUL terminated");
                std::string text(
                    reinterpret_cast<const char *>(bytes.data() + text_offset),
                    info_size - 1);
                expect(result.info.emplace(info_id, std::move(text)).second,
                       "WAVE contains duplicate INFO fields");
                info_offset = text_offset + info_size + (info_size & 1U);
            }
        }
        offset = payload_offset + payload_size + (payload_size & 1U);
    }
    expect(!result.data.empty(), "WAVE container has no audio data");
    return result;
}

[[nodiscard]] contract::Sha256Digest digest_from_hex(std::string_view hex) {
    expect(hex.size() == 64, "pinned SHA-256 text has the wrong length");
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw std::runtime_error{"pinned SHA-256 text is not lowercase hexadecimal"};
    };
    contract::Sha256Digest result;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        result.bytes[index] = static_cast<std::uint8_t>((nibble(hex[index * 2]) << 4U) |
                                                        nibble(hex[index * 2 + 1]));
    }
    return result;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

void expect_exact_random_plan(const contract::RenderManifestContent &content,
                              const RenderSpecification &specification,
                              const contract::RenderScenario &scenario,
                              const P18ReferenceCatalogV1 &catalog) {
    auto compiled =
        contract::compile_random_plan(specification.randomness, specification.engine,
                                      specification.presentation, scenario);
    const auto *expected = std::get_if<contract::RandomPlan>(&compiled);
    expect(expected != nullptr && content.randomness == *expected,
           "manifest random plan differs from exact request recompilation");

    for (const auto &seed : catalog.expected_executed_seeds) {
        const auto route = expected_route(catalog, seed.route).expected_route_id;
        const auto kind =
            seed.component == P18ReferenceExecutedRandomComponent::air_noise
                ? contract::RandomComponentKind::presentation_air_noise
                : contract::RandomComponentKind::presentation_jitter;
        const auto found = std::ranges::find_if(
            content.randomness.component_seeds, [&](const auto &actual) {
                return actual.kind == kind && actual.route_id == route;
            });
        expect(found != content.randomness.component_seeds.end() &&
                   found->initial_state == seed.expected_initial_state &&
                   found->stream == seed.expected_stream,
               "job-owned presentation random seed changed");
    }
}

void expect_exact_manifest(const contract::RenderSuccess &success,
                           const RenderSpecification &specification,
                           const contract::RenderScenario &scenario,
                           const VerifyingMemorySink &sink,
                           const P18ReferenceCatalogV1 &catalog) {
    const auto &manifest = success.manifest;
    const auto &content = manifest.content;
    expect(content.schema_version == 4, "success manifest schema changed");
    expect(content.inputs.resolved ==
               contract::ResolvedRenderInputs{
                   specification.engine,
                   specification.presentation,
                   specification.randomness,
                   scenario,
               },
           "success manifest does not retain the exact resolved request");
    expect(content.provenance == specification.provenance.bundle,
           "success manifest provenance differs from the exact request bundle");
    expect(content.rates == scenario.rates,
           "success manifest rates differ from the exact scenario");
    expect(content.output_contract ==
               contract::resolve_output_contract(specification.source_matrix),
           "success manifest output contract differs from the source matrix");
    expect(content.routes ==
               std::vector<contract::RouteRecord>{
                   {
                       contract::RouteId{1},
                       "exhaust.reference.0",
                       contract::SourceRouteKind::exhaust_outlet,
                       contract::RouteDisposition::rendered,
                       "",
                       {
                           "exhaust.reference.0.dry",
                           "exhaust.reference.0.configured_ir",
                           "exhaust.reference.0.selected",
                       },
                   },
                   {
                       contract::RouteId{2},
                       "exhaust.reference.1",
                       contract::SourceRouteKind::exhaust_outlet,
                       contract::RouteDisposition::rendered,
                       "",
                       {
                           "exhaust.reference.1.dry",
                           "exhaust.reference.1.configured_ir",
                           "exhaust.reference.1.selected",
                       },
                   },
               },
           "success manifest route records are not the exact BMW source owners");
    expect(content.output_buses ==
               std::vector<contract::OutputBusRecord>{
                   {
                       "master.reference.raw",
                       contract::OutputBusKind::master_reference_raw,
                       {"master.reference.raw"},
                   },
                   {
                       "master.reference.audition",
                       contract::OutputBusKind::master_reference_audition,
                       {"master.reference.audition"},
                   },
               },
           "success manifest bus records are not the exact BMW output owners");
    expect(content.artifacts == sink.seals,
           "success manifest artifacts differ from the sink transaction");
    expect(manifest.execution.has_value(),
           "successful public render omitted execution facts");
    expect(!success.reached_target.has_value(),
           "prescribed BMW render fabricated a reached-target result");
    expect(sink.committed_manifest.has_value() &&
               sink.committed_manifest->content == manifest.content &&
               sink.committed_manifest->execution == manifest.execution,
           "returned success differs from the manifest committed to the sink");
    expect_exact_random_plan(content, specification, scenario, catalog);
}

void expect_exact_audio(const VerifyingMemorySink &sink,
                        const std::vector<std::byte> &oracle_wave_bytes,
                        const RenderSpecification &specification,
                        const contract::RenderScenario &scenario,
                        const P18ReferenceCatalogV1 &catalog) {
    expect(sink.artifacts.size() == kP18ReferenceAudioArtifactCount &&
               sink.seals.size() == kP18ReferenceAudioArtifactCount,
           "public BMW render did not seal exactly eight audio artifacts");
    for (std::size_t index = 0; index < catalog.expected_audio.size(); ++index) {
        const auto &expected_audio = catalog.expected_audio[index];
        const auto &public_golden = kPublicAudioGoldens[index];
        expect(static_cast<std::size_t>(expected_audio.audio) == index,
               "P1.8 audio catalog order changed");
        expect(public_golden.artifact == expected_audio.audio,
               "public BMW audio golden order changed");
        const auto &actual = sink.at(expected_audio.expected_role);
        expect(actual.record.has_value(), "BMW artifact was not sealed");
        const auto &record = *actual.record;
        const auto expected_path =
            "audio/" + std::string{expected_audio.expected_role} + ".wav";
        expect(record.relative_path == expected_path &&
                   record.diagnostic == expected_audio.expected_diagnostic,
               "job-owned BMW artifact path or diagnostic policy changed");
        expect(record.byte_count == public_golden.byte_count &&
                   record.payload_sha256 ==
                       digest_from_hex(public_golden.container_sha256),
               "public BMW artifact differs from its live-simulation golden");
    }

    const auto &expected_oracle =
        *catalog.find_expected_audio(P18ReferenceAudioArtifact::master_audition);
    expect(oracle_wave_bytes.size() == expected_oracle.expected_byte_count &&
               contract::sha256(oracle_wave_bytes) == expected_oracle.expected_sha256,
           "BMW audition oracle differs from its frozen catalog identity");

    const auto &audition = sink.at(expected_oracle.expected_role);
    expect(audition.record.has_value(), "public BMW audition artifact was not sealed");

    const auto actual_wave = parse_wave(audition.bytes);
    const auto oracle_wave = parse_wave(oracle_wave_bytes);
    const auto *expected_pcm =
        catalog.find_expected_mastering(P18ReferenceMasteringPayload::pcm24le);
    const auto actual_pcm_sha256 = contract::sha256(actual_wave.data);
    if (actual_pcm_sha256 != digest_from_hex(kExpectedPublicAuditionPcm24Sha256)) {
        std::cerr << "public BMW audition PCM24 SHA-256: "
                  << digest_hex(actual_pcm_sha256) << '\n';
        throw std::runtime_error{
            "public BMW audition decoded PCM differs from its golden"};
    }
    expect(expected_pcm != nullptr && actual_wave.data.size() == UINT64_C(8640000) &&
               actual_wave.data.size() == oracle_wave.data.size() &&
               contract::sha256(oracle_wave.data) == expected_pcm->expected_sha256,
           "historical BMW oracle decoded PCM differs from its frozen comparator");

    const auto &method = specification.presentation.methods.audition_mix.value;
    const auto expected_comment =
        "engine=" + specification.engine.engine_id.value +
        ";profile=" + specification.engine.profile_id.value +
        ";scenario=" + scenario.scenario_id +
        ";presentation=" + specification.presentation.calibration_id +
        ";source_matrix=" + specification.source_matrix.id;
    const auto expected_title = "engine=" + specification.engine.engine_id.value +
                                ";scenario=" + scenario.scenario_id;
    const auto expected_software =
        "engine-sim-offline;method=" + method.id +
        ";version=" + std::to_string(method.version) +
        ";configuration_sha256=" + digest_hex(method.configuration_sha256);
    expect(actual_wave.info.size() == 3 &&
               actual_wave.info.at("ICMT") == expected_comment &&
               actual_wave.info.at("INAM") == expected_title &&
               actual_wave.info.at("ISFT") == expected_software,
           "public BMW audition INFO metadata is not exact job-owned evidence");
}

void run(const std::filesystem::path &fixture_root,
         const std::filesystem::path &oracle_wave_path) {
    const auto started = std::chrono::steady_clock::now();
    const auto &catalog = p18_reference_catalog_v1();
    auto request = make_public_request(fixture_root, catalog);

    contract::Sha256Digest verified_ir_sha256;
    std::uint64_t verified_ir_byte_count = 0;
    {
        // This verifies all immutable lineage, but its captured pressure/audit lane is
        // deliberately destroyed here and cannot become an execution input.
        auto fixture = load_p18_reference_fixture(fixture_root);
        const auto &identity =
            fixture.verified_lineage.at(P18ReferenceLineageFile::configured_ir_input);
        verified_ir_sha256 = identity.payload_sha256;
        verified_ir_byte_count = identity.byte_count;
    }

    const auto &ir_catalog = catalog.expected_lineage_files[static_cast<std::size_t>(
        P18ReferenceLineageFile::configured_ir_input)];
    expect(ir_catalog.file == P18ReferenceLineageFile::configured_ir_input &&
               verified_ir_byte_count == ir_catalog.expected_byte_count &&
               verified_ir_sha256 == ir_catalog.expected_sha256,
           "verified configured-IR identity differs from the presentation catalog");
    auto ir_bytes = read_bytes(fixture_root / ir_catalog.expected_relative_path,
                               verified_ir_byte_count);
    expect(contract::sha256(ir_bytes) == verified_ir_sha256,
           "configured-IR payload changed after verified fixture preflight");

    auto specification = make_specification(request, catalog, std::move(ir_bytes));
    expect(specification.engine == request.engine &&
               specification.asset_payloads.size() == 1 &&
               specification.asset_payloads.front().bytes.size() ==
                   verified_ir_byte_count &&
               contract::sha256(specification.asset_payloads.front().bytes) ==
                   verified_ir_sha256,
           "render specification is not based on the exact public BMW request and IR");
    expect_valid(contract::validate_render_admission(
                     specification.engine, specification.presentation,
                     specification.randomness, request.scenario,
                     specification.provenance, specification.source_matrix),
                 "public BMW render admission failed");

    VerifyingMemorySink sink;
    const auto render_started = std::chrono::steady_clock::now();
    const auto result = render(specification, request.scenario, sink);
    const auto render_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - render_started);
    const auto *success = std::get_if<contract::RenderSuccess>(&result);
    if (success == nullptr) {
        const auto *failure = std::get_if<contract::RenderFailure>(&result);
        if (failure != nullptr) {
            throw std::runtime_error{"public BMW render failed (" +
                                     failure->context.detail_code +
                                     "): " + failure->context.state_summary + "; " +
                                     report_text(failure->validation)};
        }
        throw std::runtime_error{"public BMW render returned unreachable target"};
    }

    expect(sink.begin_calls == 1 &&
               sink.declaration_calls == kP18ReferenceAudioArtifactCount &&
               sink.write_calls > 0 &&
               sink.seal_calls == kP18ReferenceAudioArtifactCount &&
               sink.commit_calls == 1 && sink.abort_calls == 0,
           "successful public BMW render violated sink transaction cardinality");
    expect_valid(validate(result, specification, request.scenario),
                 "public render-layer result validation failed");
    expect_exact_manifest(*success, specification, request.scenario, sink, catalog);

    const auto oracle_wave_bytes = read_bytes(
        oracle_wave_path,
        catalog.find_expected_audio(P18ReferenceAudioArtifact::master_audition)
            ->expected_byte_count);
    expect_exact_audio(sink, oracle_wave_bytes, specification, request.scenario,
                       catalog);

    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    std::cout << "public BMW render: " << render_elapsed.count()
              << " s; complete integration: " << elapsed.count() << " s\n";
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 3,
               "usage: bmw_public_render_success_test <fixture-root> <oracle-wave>");
        run(std::filesystem::path{argv[1]}, std::filesystem::path{argv[2]});
    } catch (const std::exception &error) {
        std::cerr << "BMW public render success integration failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
