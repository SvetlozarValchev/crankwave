#include "bmw_m52b28_render_gate_support.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace engine_sim_offline::test::bmw_m52b28_render_gate {
namespace {

constexpr std::string_view kLastGoodWaveSha256 =
    "87eda586902fbcf7e015161a84688c74e486285c99150c1a6fb3bc9c4382c444";
constexpr std::string_view kAcceptedPcm24Sha256 =
    "176010069c88c99a3cc8262099fa5f02eba3af9517b1c92e148d88ace869756f";
constexpr std::uint64_t kAcceptedPcm24ByteCount = UINT64_C(8640000);
constexpr std::uint64_t kAudibleFrameCount = UINT64_C(2880000);

[[nodiscard]] std::uint32_t read_u32le(const std::span<const std::byte> bytes,
                                       const std::size_t offset) {
    expect(offset <= bytes.size() && bytes.size() - offset >= 4U,
           "WAVE u32 read was out of range");
    std::uint32_t result = 0;
    for (unsigned index = 0; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << (index * 8U);
    }
    return result;
}

[[nodiscard]] bool fourcc_is(const std::span<const std::byte> bytes,
                             const std::size_t offset,
                             const std::string_view expected) {
    if (expected.size() != 4U || offset > bytes.size() || bytes.size() - offset < 4U) {
        return false;
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        if (std::to_integer<unsigned char>(bytes[offset + index]) !=
            static_cast<unsigned char>(expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string fourcc(const std::span<const std::byte> bytes,
                                 const std::size_t offset) {
    expect(offset <= bytes.size() && bytes.size() - offset >= 4U,
           "WAVE FourCC read was out of range");
    std::string result(4U, '\0');
    for (std::size_t index = 0; index < 4U; ++index) {
        result[index] =
            static_cast<char>(std::to_integer<unsigned char>(bytes[offset + index]));
    }
    return result;
}

struct ParsedWave {
    std::span<const std::byte> data;
    std::unordered_map<std::string, std::string> info;
};

[[nodiscard]] ParsedWave parse_wave(const std::span<const std::byte> bytes) {
    expect(bytes.size() >= 12U && fourcc_is(bytes, 0U, "RIFF") &&
               fourcc_is(bytes, 8U, "WAVE"),
           "artifact is not a RIFF/WAVE container");
    expect(static_cast<std::uint64_t>(read_u32le(bytes, 4U)) + 8U == bytes.size(),
           "RIFF size does not describe the complete WAVE container");

    ParsedWave result;
    for (std::size_t offset = 12U; offset < bytes.size();) {
        expect(bytes.size() - offset >= 8U, "truncated top-level WAVE chunk");
        const auto id = fourcc(bytes, offset);
        const auto payload_size =
            static_cast<std::size_t>(read_u32le(bytes, offset + 4U));
        const auto payload_offset = offset + 8U;
        expect(payload_offset <= bytes.size() &&
                   payload_size <= bytes.size() - payload_offset,
               "top-level WAVE chunk exceeds the container");

        if (id == "data") {
            expect(result.data.empty(), "WAVE contains duplicate data chunks");
            result.data = bytes.subspan(payload_offset, payload_size);
        } else if (id == "LIST" && payload_size >= 4U &&
                   fourcc_is(bytes, payload_offset, "INFO")) {
            const auto list_end = payload_offset + payload_size;
            for (std::size_t info_offset = payload_offset + 4U;
                 info_offset < list_end;) {
                expect(list_end - info_offset >= 8U, "truncated INFO subchunk");
                const auto info_id = fourcc(bytes, info_offset);
                const auto info_size =
                    static_cast<std::size_t>(read_u32le(bytes, info_offset + 4U));
                const auto text_offset = info_offset + 8U;
                expect(text_offset <= list_end && info_size > 0U &&
                           info_size <= list_end - text_offset,
                       "INFO subchunk exceeds its LIST");
                expect(bytes[text_offset + info_size - 1U] == std::byte{0},
                       "INFO text is not NUL terminated");
                std::string text{
                    reinterpret_cast<const char *>(bytes.data() + text_offset),
                    info_size - 1U,
                };
                expect(result.info.emplace(info_id, std::move(text)).second,
                       "WAVE contains duplicate INFO fields");
                info_offset = text_offset + info_size + (info_size & std::size_t{1});
            }
        }
        offset = payload_offset + payload_size + (payload_size & std::size_t{1});
    }
    expect(!result.data.empty(), "WAVE container has no audio data");
    return result;
}

struct ArtifactExpectation {
    std::string_view role;
    std::string_view encoding;
    bool diagnostic = false;
};

constexpr std::array<ArtifactExpectation, 8U> kArtifacts{{
    {"exhaust.reference.0.dry", "float32le", true},
    {"exhaust.reference.0.configured_ir", "float32le", true},
    {"exhaust.reference.0.selected", "float32le", false},
    {"exhaust.reference.1.dry", "float32le", true},
    {"exhaust.reference.1.configured_ir", "float32le", true},
    {"exhaust.reference.1.selected", "float32le", false},
    {"master.engine.raw", "float32le", false},
    {"master.engine.audition", "pcm_s24le", false},
}};

void expect_exact_artifacts(const contract::RenderManifestContent &content,
                            const VerifyingMemorySink &sink) {
    expect(sink.artifacts.size() == kArtifacts.size() &&
               sink.seals.size() == kArtifacts.size() &&
               content.artifacts.size() == kArtifacts.size(),
           "generic render did not publish exactly eight audio artifacts");
    for (const auto &expected : kArtifacts) {
        const auto &artifact = sink.at(expected.role);
        expect(artifact.record.has_value(), "generic artifact was not sealed");
        const auto &record = *artifact.record;
        const auto expected_path = "audio/" + std::string{expected.role} + ".wav";
        const contract::AudioContract expected_audio{
            {192000U, 1U},
            kAudibleFrameCount,
            "mono",
            std::string{expected.encoding},
        };
        expect(record.role == expected.role &&
                   record.kind == contract::ArtifactKind::audio &&
                   record.relative_path == expected_path &&
                   record.audio == expected_audio &&
                   record.diagnostic == expected.diagnostic,
               "generic artifact role, path, media, or policy changed");
        expect(artifact.declaration.role == record.role &&
                   artifact.declaration.kind == record.kind &&
                   artifact.declaration.relative_path == record.relative_path &&
                   artifact.declaration.audio == record.audio &&
                   artifact.declaration.diagnostic == record.diagnostic,
               "generic artifact declaration differed from its manifest record");
    }
}

void expect_exact_manifest(const contract::RenderSuccess &success,
                           const VerifyingMemorySink &sink) {
    const auto &manifest = success.manifest;
    const auto &content = manifest.content;
    expect(content.schema_version == 8U, "generic render manifest schema changed");
    expect(sink.output_contract == content.output_contract &&
               content.output_contract.source_matrix_id ==
                   "scenario-source-matrix."
                   "bmw-m52b28-inertial-dyno-1500-6500rpm",
           "generic source-matrix output contract changed");
    expect(content.routes ==
               std::vector<contract::RouteRecord>{
                   {
                       contract::RouteId{1U},
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
                       contract::RouteId{2U},
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
           "generic manifest route owners changed");
    expect(content.output_buses ==
               std::vector<contract::OutputBusRecord>{
                   {
                       "master.engine.audition",
                       contract::OutputBusKind::master_engine_audition,
                       {"master.engine.audition"},
                   },
                   {
                       "master.engine.raw",
                       contract::OutputBusKind::master_engine_raw,
                       {"master.engine.raw"},
                   },
               },
           "generic manifest output-bus owners changed");
    expect(content.artifacts == sink.seals,
           "manifest artifacts differ from the sink transaction");
    expect(sink.committed_manifest.has_value() &&
               sink.committed_manifest->content == content &&
               sink.committed_manifest->execution == manifest.execution,
           "returned manifest differs from the committed manifest");
    expect_exact_artifacts(content, sink);
}

void expect_exact_info(const contract::RenderManifestContent &content,
                       const ParsedWave &audition) {
    const auto &inputs = content.inputs.resolved;
    const auto &method = inputs.presentation.methods.audition_mix.value;
    const auto expected_comment =
        "engine=" + inputs.engine.engine_id.value +
        ";profile=" + inputs.engine.profile_id.value +
        ";scenario=" + inputs.scenario.scenario_id +
        ";presentation=" + inputs.presentation.calibration_id +
        ";source_matrix=" + content.output_contract.source_matrix_id;
    const auto expected_title = "engine=" + inputs.engine.engine_id.value +
                                ";scenario=" + inputs.scenario.scenario_id;
    const auto expected_software =
        "engine-sim-offline;method=" + method.id +
        ";version=" + std::to_string(method.version) +
        ";configuration_sha256=" + digest_hex(method.configuration_sha256);
    expect(audition.info.size() == 3U && audition.info.at("ICMT") == expected_comment &&
               audition.info.at("INAM") == expected_title &&
               audition.info.at("ISFT") == expected_software,
           "generic audition INFO is not exact manifest-owned evidence");
}

} // namespace

RenderIdentityObservation
verify_render_success(const contract::RenderSuccess &success,
                      const VerifyingMemorySink &sink,
                      const std::span<const std::byte> last_good_oracle_wave) {
    expect(sink.begin_calls == 1U && sink.declaration_calls == kArtifacts.size() &&
               sink.write_calls > 0U && sink.seal_calls == kArtifacts.size() &&
               sink.commit_calls == 1U && sink.abort_calls == 0U,
           "successful generic render violated sink transaction cardinality");
    expect(!success.reached_target.has_value() &&
               !success.held_speed_operating_point.has_value() &&
               success.inertial_dyno.has_value(),
           "generic inertial render omitted its exclusive success evidence");
    const auto &inertial = *success.inertial_dyno;
    expect(inertial.start_engine_speed_rpm == 1500.0 &&
               inertial.target_engine_speed_rpm == 6500.0 &&
               inertial.first_target_reached_frame_index.has_value(),
           "generic inertial render did not complete the authored dyno pull");
    expect_exact_manifest(success, sink);

    expect(last_good_oracle_wave.size() == UINT64_C(8640578) &&
               contract::sha256(last_good_oracle_wave) ==
                   digest_from_hex(kLastGoodWaveSha256),
           "render gate did not receive the authoritative last-good WAV");
    const auto oracle = parse_wave(last_good_oracle_wave);
    expect(oracle.data.size() == kAcceptedPcm24ByteCount &&
               contract::sha256(oracle.data) == digest_from_hex(kAcceptedPcm24Sha256),
           "authoritative last-good PCM24 span changed");

    const auto &audition_artifact = sink.at("master.engine.audition");
    const auto audition = parse_wave(audition_artifact.bytes);
    expect(audition.data.size() == kAcceptedPcm24ByteCount &&
               contract::sha256(audition.data) ==
                   digest_from_hex(kAcceptedPcm24Sha256) &&
               std::ranges::equal(audition.data, oracle.data),
           "generic PCM24 data span differs from the accepted last-good bytes");
    expect_exact_info(success.manifest.content, audition);

    return {
        digest_hex(inertial.simulation_request_identity_v5_sha256),
        static_cast<std::uint64_t>(audition_artifact.bytes.size()),
        digest_hex(contract::sha256(audition_artifact.bytes)),
    };
}

} // namespace engine_sim_offline::test::bmw_m52b28_render_gate
