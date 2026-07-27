#include "artifacts/p18_audition_wav_encoder.hpp"
#include "contract/sha256_stream.hpp"
#include "determinism/renderer_determinism_envelope.hpp"
#include "engine_sim_offline/artifacts/wav_encoder.hpp"
#include "engine_sim_offline/contract/render_manifest.hpp"
#include "presentation/p18_mastering.hpp"
#include "reference/p18_reference_artifact_set.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_manifest_content.hpp"
#include "reference/p18_reference_method_identity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <unistd.h>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::artifacts;
using namespace engine_sim_offline::determinism;
using namespace engine_sim_offline::presentation;
using namespace engine_sim_offline::reference;

using ManifestContentBuilder = decltype(make_p18_reference_manifest_content);
using ArtifactRecordArray =
    std::array<P18ReferenceArtifactRecord, kP18ReferenceAudioArtifactCount>;

static_assert(!std::is_default_constructible_v<P18ReferenceManifestContent>);
static_assert(
    !std::is_constructible_v<P18ReferenceManifestContent, P18ReferenceProvenance,
                             contract::RenderManifestContent>);
static_assert(std::is_copy_constructible_v<P18ReferenceManifestContent>);
static_assert(std::is_move_constructible_v<P18ReferenceManifestContent>);
static_assert(!std::is_copy_assignable_v<P18ReferenceManifestContent>);
static_assert(!std::is_move_assignable_v<P18ReferenceManifestContent>);
static_assert(!std::is_invocable_v<
              ManifestContentBuilder, const P18ReferenceCatalogV1 &,
              const P18ReferenceArtifactSet &, const RendererDeterminismEnvelope &>);
static_assert(!std::is_invocable_v<
              ManifestContentBuilder, const P18LoadedReferenceFixture &,
              const ArtifactRecordArray &, const RendererDeterminismEnvelope &>);
static_assert(!std::is_invocable_v<
              ManifestContentBuilder, const P18LoadedReferenceFixture &,
              const P18ReferenceArtifactSet &, const contract::DeterminismEnvelope &>);

constexpr std::size_t kIoBlockBytes = 64U * 1024U;
constexpr std::size_t kMasterBlockFrames = 9'600;
constexpr std::size_t kFloatWaveHeaderBytes = 58;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class Function>
void expect_exception(Function &&function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error{std::string{message}};
}

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        constexpr std::string_view pattern =
            "/tmp/engine-sim-offline-p18-manifest-content-XXXXXX";
        std::array<char, 80> writable{};
        expect(pattern.size() + 1U <= writable.size(),
               "temporary-directory pattern overflow");
        std::copy(pattern.begin(), pattern.end(), writable.begin());
        const auto *created = ::mkdtemp(writable.data());
        expect(created != nullptr, "could not create temporary test directory");
        path_ = created;
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::unique_ptr<P18ReferenceArtifactSet>
require_artifact_set(P18ReferenceArtifactSet::CreateResult result) {
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error{error->detail_code + ": " + error->message};
    }
    return std::move(std::get<std::unique_ptr<P18ReferenceArtifactSet>>(result));
}

void require_sink_success(const RenderSinkStatus &status, std::string_view operation) {
    if (status.has_value()) {
        throw std::runtime_error{std::string{operation} + ": " + status->detail_code +
                                 ": " + status->message};
    }
}

void require_encoding_success(const WavEncodingStatus &status,
                              std::string_view operation) {
    if (status.has_value()) {
        throw std::runtime_error{std::string{operation} + ": " + status->path + ": " +
                                 status->message};
    }
}

[[nodiscard]] WavEncoder require_float_encoder() {
    const contract::AudioContract audio{
        {192'000, 1}, kP18AudibleFrameCount, "mono", "float32le"};
    auto result = make_wav_encoder(audio, {16U * 1024U});
    if (const auto *error = std::get_if<WavEncodingError>(&result)) {
        throw std::runtime_error{"could not construct raw-master encoder: " +
                                 error->message};
    }
    return std::get<WavEncoder>(std::move(result));
}

[[nodiscard]] P18AuditionWaveEncoder require_audition_encoder() {
    auto result = make_p18_audition_wave_encoder({16U * 1024U});
    if (const auto *error = std::get_if<WavEncodingError>(&result)) {
        throw std::runtime_error{"could not construct audition encoder: " +
                                 error->message};
    }
    return std::get<P18AuditionWaveEncoder>(std::move(result));
}

[[nodiscard]] std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw std::runtime_error{"invalid frozen-header hex digit"};
}

[[nodiscard]] std::vector<std::byte> bytes_from_hex(std::string_view text) {
    expect(text.size() % 2U == 0U, "frozen-header hex has odd length");
    std::vector<std::byte> result;
    result.reserve(text.size() / 2U);
    for (std::size_t index = 0; index < text.size(); index += 2U) {
        result.push_back(static_cast<std::byte>((hex_nibble(text[index]) << 4U) |
                                                hex_nibble(text[index + 1U])));
    }
    return result;
}

[[nodiscard]] std::uint32_t read_u32le(std::span<const std::byte> bytes,
                                       std::size_t offset) {
    std::uint32_t result = 0;
    for (unsigned index = 0; index < 4U; ++index) {
        result |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return result;
}

class SelectedStemReader final {
  public:
    explicit SelectedStemReader(const std::filesystem::path &path)
        : stream_(path, std::ios::binary | std::ios::ate), path_(path.string()) {
        expect(static_cast<bool>(stream_), "could not open selected stem");
        const auto observed_size = stream_.tellg();
        expect(observed_size >= 0 &&
                   static_cast<std::uint64_t>(observed_size) == 11'520'058,
               "selected stem has the wrong byte count");
        stream_.seekg(0);

        std::array<std::byte, kFloatWaveHeaderBytes> header{};
        stream_.read(reinterpret_cast<char *>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        expect(stream_.gcount() == static_cast<std::streamsize>(header.size()),
               "selected stem header is truncated");
        const auto expected = bytes_from_hex(
            "5249464632c8af0057415645666d7420120000000300010000ee020000b80b00040020"
            "000000666163740400000000f22b006461746100c8af00");
        expect(std::ranges::equal(header, expected),
               "selected stem has the wrong Float32 WAVE header");
    }

    [[nodiscard]] std::size_t read(std::span<float> output) {
        const auto remaining =
            static_cast<std::size_t>(kP18AudibleFrameCount - frames_read_);
        const auto count = std::min(output.size(), remaining);
        if (count == 0) {
            return 0;
        }
        bytes_.resize(count * sizeof(float));
        stream_.read(reinterpret_cast<char *>(bytes_.data()),
                     static_cast<std::streamsize>(bytes_.size()));
        if (stream_.gcount() != static_cast<std::streamsize>(bytes_.size())) {
            throw std::runtime_error{"selected stem payload is truncated: " + path_};
        }
        for (std::size_t index = 0; index < count; ++index) {
            output[index] =
                std::bit_cast<float>(read_u32le(bytes_, index * sizeof(float)));
            if (!std::isfinite(output[index])) {
                throw std::runtime_error{
                    "selected stem contains a non-finite sample: " + path_};
            }
        }
        frames_read_ += count;
        return count;
    }

    [[nodiscard]] std::uint64_t frames_read() const noexcept {
        return frames_read_;
    }

  private:
    std::ifstream stream_;
    std::string path_;
    std::vector<std::byte> bytes_;
    std::uint64_t frames_read_ = 0;
};

void copy_file_to_artifact(const std::filesystem::path &path,
                           std::uint64_t expected_byte_count,
                           const WavChunkConsumer &consumer) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    expect(static_cast<bool>(stream), "could not open frozen stem");
    const auto observed_size = stream.tellg();
    expect(observed_size >= 0 &&
               static_cast<std::uint64_t>(observed_size) == expected_byte_count,
           "frozen stem has the wrong byte count");
    stream.seekg(0);

    std::array<std::byte, kIoBlockBytes> bytes{};
    std::uint64_t offset = 0;
    while (offset < expected_byte_count) {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(bytes.size(), expected_byte_count - offset));
        stream.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(count));
        expect(stream.gcount() == static_cast<std::streamsize>(count),
               "frozen stem read was truncated");
        expect(consumer(offset, std::span<const std::byte>{bytes}.first(count)),
               "artifact set rejected a frozen stem chunk");
        offset += count;
    }
}

struct ObservedFileIdentity {
    std::uint64_t byte_count = 0;
    contract::Sha256Digest payload_sha256;
};

[[nodiscard]] ObservedFileIdentity
observe_file_identity(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    expect(static_cast<bool>(stream), "could not open audition oracle");

    contract::detail::Sha256Stream hash;
    std::array<std::byte, kIoBlockBytes> bytes{};
    std::uint64_t byte_count = 0;
    while (stream) {
        stream.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            hash.update(std::span<const std::byte>{bytes}.first(
                static_cast<std::size_t>(count)));
            byte_count += static_cast<std::uint64_t>(count);
        }
    }
    expect(stream.eof(), "audition oracle read failed");
    return {byte_count, hash.finish()};
}

struct StemInput {
    P18ReferenceAudioArtifact artifact;
    std::string_view filename;
};

constexpr std::array<StemInput, 6> kStemInputs{{
    {P18ReferenceAudioArtifact::exhaust_0_dry, "exhaust-0-linear-dry.wav"},
    {P18ReferenceAudioArtifact::exhaust_0_configured_ir,
     "exhaust-0-linear-configured-ir.wav"},
    {P18ReferenceAudioArtifact::exhaust_0_selected, "exhaust-0-linear-wet-dry.wav"},
    {P18ReferenceAudioArtifact::exhaust_1_dry, "exhaust-1-linear-dry.wav"},
    {P18ReferenceAudioArtifact::exhaust_1_configured_ir,
     "exhaust-1-linear-configured-ir.wav"},
    {P18ReferenceAudioArtifact::exhaust_1_selected, "exhaust-1-linear-wet-dry.wav"},
}};

void write_frozen_stems(P18ReferenceArtifactSet &artifacts,
                        const std::filesystem::path &stems_root) {
    const auto &catalog = p18_reference_catalog_v1();
    for (const auto &input : kStemInputs) {
        const auto *expected = catalog.find_expected_audio(input.artifact);
        expect(expected != nullptr, "catalog is missing a frozen stem");
        copy_file_to_artifact(stems_root / input.filename,
                              expected->expected_byte_count,
                              artifacts.consumer(input.artifact));
        require_sink_success(artifacts.seal(input.artifact),
                             "could not seal frozen stem");
    }
}

void write_derived_masters(P18ReferenceArtifactSet &artifacts,
                           const std::filesystem::path &stems_root) {
    SelectedStemReader route_0{stems_root / "exhaust-0-linear-wet-dry.wav"};
    SelectedStemReader route_1{stems_root / "exhaust-1-linear-wet-dry.wav"};
    auto raw_encoder = require_float_encoder();
    auto audition_encoder = require_audition_encoder();
    const auto raw_consumer = artifacts.consumer(P18ReferenceAudioArtifact::master_raw);
    const auto audition_consumer =
        artifacts.consumer(P18ReferenceAudioArtifact::master_audition);

    require_encoding_success(raw_encoder.begin(raw_consumer),
                             "raw-master header emission failed");
    require_encoding_success(audition_encoder.begin(audition_consumer),
                             "audition-master header emission failed");

    std::array<float, kMasterBlockFrames> route_0_samples{};
    std::array<float, kMasterBlockFrames> route_1_samples{};
    std::array<float, kMasterBlockFrames> raw_samples{};
    std::array<std::int32_t, kMasterBlockFrames> pcm24_samples{};
    std::array<P18MasteredFrame, kMasterBlockFrames> mastered{};
    std::uint64_t first_frame = 0;
    while (first_frame < kP18AudibleFrameCount) {
        const auto count_0 = route_0.read(route_0_samples);
        const auto count_1 = route_1.read(route_1_samples);
        expect(count_0 > 0 && count_0 == count_1, "selected-stem block lengths differ");
        p18_master_reference_block(std::span{route_0_samples}.first(count_0),
                                   std::span{route_1_samples}.first(count_0),
                                   first_frame, std::span{mastered}.first(count_0));
        for (std::size_t index = 0; index < count_0; ++index) {
            raw_samples[index] = mastered[index].raw;
            pcm24_samples[index] = mastered[index].pcm24;
        }
        require_encoding_success(
            raw_encoder.write_float32_interleaved(std::span{raw_samples}.first(count_0),
                                                  raw_consumer),
            "raw-master payload emission failed");
        require_encoding_success(
            audition_encoder.write_pcm24(std::span{pcm24_samples}.first(count_0),
                                         audition_consumer),
            "audition-master payload emission failed");
        first_frame += count_0;
    }
    expect(route_0.frames_read() == kP18AudibleFrameCount &&
               route_1.frames_read() == kP18AudibleFrameCount,
           "selected stems did not cover the complete audible interval");

    require_encoding_success(raw_encoder.finish(raw_consumer),
                             "raw-master finalization failed");
    require_encoding_success(audition_encoder.finish(audition_consumer),
                             "audition-master finalization failed");
    require_sink_success(artifacts.seal(P18ReferenceAudioArtifact::master_raw),
                         "could not seal raw master");
    require_sink_success(artifacts.seal(P18ReferenceAudioArtifact::master_audition),
                         "could not seal audition master");
}

[[nodiscard]] std::unique_ptr<P18ReferenceArtifactSet>
make_exact_artifacts(const std::filesystem::path &temporary_root,
                     const std::filesystem::path &stems_root) {
    auto artifacts =
        require_artifact_set(P18ReferenceArtifactSet::create(temporary_root, "exact"));
    write_frozen_stems(*artifacts, stems_root);
    write_derived_masters(*artifacts, stems_root);
    return artifacts;
}

[[nodiscard]] std::unique_ptr<P18ReferenceArtifactSet>
make_wrong_artifacts(const std::filesystem::path &temporary_root) {
    auto artifacts =
        require_artifact_set(P18ReferenceArtifactSet::create(temporary_root, "wrong"));
    std::array<std::byte, kIoBlockBytes> zeroes{};
    for (const auto &expected : p18_reference_catalog_v1().expected_audio) {
        const auto consumer = artifacts->consumer(expected.audio);
        std::uint64_t offset = 0;
        while (offset < expected.expected_byte_count) {
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                zeroes.size(), expected.expected_byte_count - offset));
            expect(consumer(offset, std::span<const std::byte>{zeroes}.first(count)),
                   "wrong-byte fixture was rejected before sealing");
            offset += count;
        }
        require_sink_success(artifacts->seal(expected.audio),
                             "wrong-byte fixture could not be sealed");
        const auto observed = artifacts->record(expected.audio);
        expect(observed.has_value() &&
                   observed->payload_sha256 != expected.expected_sha256,
               "wrong-byte fixture accidentally matched an expected artifact");
    }
    return artifacts;
}

[[nodiscard]] RendererNumericEnvironmentSnapshot fake_numeric_snapshot() noexcept {
    RendererNumericEnvironmentSnapshot snapshot;
    snapshot.build_policy = RendererNumericBuildPolicy::x86_64_v1_strict_v1;
    snapshot.is_linux_x86_64 = true;
    snapshot.is_sysv_lp64 = true;
    snapshot.pointer_storage_bytes = 8;
    snapshot.long_storage_bytes = 8;
    snapshot.arch_get_cpuid_observed = true;
    snapshot.cpuid_enabled = true;
    snapshot.cpuid_leaf1_observed = true;
    snapshot.maximum_basic_cpuid_leaf = 1;
    snapshot.cpuid_leaf1_edx = determinism::detail::kRequiredCpuidLeaf1Edx;
    snapshot.binary32 = {2, 24, -125, 128, 4, true};
    snapshot.binary64 = {2, 53, -1021, 1024, 8, true};
    snapshot.extended80 = {2, 64, -16381, 16384, 16, true};
    snapshot.float_evaluation_method = 0;
    snapshot.fe_rounding_mode = FE_TONEAREST;
    snapshot.mxcsr = determinism::detail::kRequiredMxcsrControl;
    snapshot.x87_control_word = determinism::detail::kRequiredX87Control;
    return snapshot;
}

[[nodiscard]] RendererSourceStampResult fake_source_stamp() {
    RendererSourceStamp stamp;
    stamp.source_state = RendererSourceState::clean;
    stamp.full_git_head = "0123456789abcdef0123456789abcdef01234567";
    stamp.source_closure_sha256.bytes[0] = 0x42;
    stamp.compiler_id = "GNU";
    stamp.compiler_version = "13.3.0";
    stamp.target_triple = "x86_64-linux-gnu";
    return stamp;
}

[[nodiscard]] std::string provider_identity(std::string_view soname) {
    return "elf64le-x86_64.soname." + std::string{soname} +
           ".bytes.4096.buildid."
           "0100000000000000000000000000000000000000.sha256."
           "0200000000000000000000000000000000000000000000000000000000000000";
}

[[nodiscard]] LoadedRuntimeIdentityResult fake_loaded_runtime() {
    LoadedRuntimeIdentity identity;
    identity.standard_library_id = "libstdcxx";
    identity.standard_library_identity =
        "release.13.headers.20240601.gxxabi.1019.cxx11abi.1+" +
        provider_identity("libstdc++.so.6") +
        "+symbols.__cxa_throw.CXXABI_1.3.00000000000bb340";
    identity.math_library_id = "glibc-libm";
    identity.math_library_identity = "glibc.2.39+" + provider_identity("libm.so.6") +
                                     "+symbols.ceil.GLIBC_2.2.5.0000000000000001"
                                     ".cos.GLIBC_2.2.5.0000000000000002"
                                     ".floor.GLIBC_2.2.5.0000000000000003"
                                     ".roundl.GLIBC_2.2.5.0000000000000004"
                                     ".sin.GLIBC_2.2.5.0000000000000005"
                                     ".sincos.GLIBC_2.2.5.0000000000000006"
                                     ".tan.GLIBC_2.2.5.0000000000000007";
    identity.compiler_runtime_id = "libgcc-s";
    identity.compiler_runtime_identity = provider_identity("libgcc_s.so.1") +
                                         "+symbols.__muldc3.GCC_4.0.0.0000000000000001";
    return identity;
}

[[nodiscard]] RendererDeterminismEnvelope make_fake_renderer_identity() {
    auto result = determinism::detail::compose_renderer_determinism_envelope(
        {&fake_numeric_snapshot, &fake_source_stamp, &fake_loaded_runtime});
    if (auto *envelope = std::get_if<RendererDeterminismEnvelope>(&result)) {
        return std::move(*envelope);
    }
    throw std::runtime_error{"valid fake renderer identity was not sealed"};
}

void test_scripted_renderer_identity_is_rejected(
    const P18LoadedReferenceFixture &fixture,
    const std::filesystem::path &temporary_root) {
    const auto scripted_identity = make_fake_renderer_identity();
    expect(!scripted_identity.production_observation(),
           "scripted renderer identity claimed production authority");
    auto artifacts = require_artifact_set(
        P18ReferenceArtifactSet::create(temporary_root, "scripted"));
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(fixture, *artifacts,
                                                                  scripted_identity));
        },
        "scripted renderer observers were accepted by manifest construction");
}

[[nodiscard]] const contract::ReferencePayloadIdentity &
lineage_payload(const contract::ReferenceFixtureIdentityV1 &fixture,
                P18ReferenceLineageFile file) {
    switch (file) {
    case P18ReferenceLineageFile::manifest:
        return fixture.manifest;
    case P18ReferenceLineageFile::parity_evidence:
        return fixture.parity_evidence;
    case P18ReferenceLineageFile::audit_input:
        return fixture.audit_input;
    case P18ReferenceLineageFile::component_seed_input:
        return fixture.component_seed_input;
    case P18ReferenceLineageFile::renderer_algorithm_record:
        return fixture.renderer_algorithm_record;
    case P18ReferenceLineageFile::configured_ir_input:
        return fixture.configured_ir_input;
    case P18ReferenceLineageFile::kernel_oracle_comparator:
        return fixture.kernel_oracle_comparator;
    }
    throw std::logic_error{"unknown lineage file"};
}

void test_exact_content(const P18LoadedReferenceFixture &fixture,
                        const P18ReferenceArtifactSet &artifacts,
                        const RendererDeterminismEnvelope &renderer_identity,
                        const std::filesystem::path &audition_oracle) {
    const auto sealed =
        make_p18_reference_manifest_content(fixture, artifacts, renderer_identity);
    const auto repeated =
        make_p18_reference_manifest_content(fixture, artifacts, renderer_identity);
    const auto &content = sealed.content();
    const auto &ledger = sealed.provenance().ledger();
    const auto &catalog = p18_reference_catalog_v1();
    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();

    expect(content == repeated.content() && ledger == repeated.provenance().ledger(),
           "reference-manifest content depends on construction history");
    expect(contract::validate(content, ledger, source_matrix).ok(),
           "constructed reference-manifest content is not contract-valid");
    expect(content.determinism == renderer_identity.manifest_identity(),
           "manifest did not retain the sealed renderer identity");
    expect(content.provenance == ledger.bundle,
           "manifest did not retain the sealed provenance bundle identity");

    const auto *inputs =
        std::get_if<contract::ReferencePresentationInputsV1>(&content.inputs);
    expect(inputs != nullptr, "manifest did not select reference-presentation inputs");
    expect(inputs->audit_reader ==
                   p18_reference_method_identity(P18ReferenceMethod::audit_reader)
                       .contract_identity() &&
               inputs->excitation_adapter ==
                   p18_reference_method_identity(P18ReferenceMethod::excitation_adapter)
                       .contract_identity() &&
               inputs->excitation_seam ==
                   p18_reference_method_identity(P18ReferenceMethod::excitation_seam)
                       .contract_identity(),
           "manifest input boundary did not retain the pinned method identities");
    for (std::size_t index = 0; index < kP18ReferenceLineageFileCount; ++index) {
        const auto file = static_cast<P18ReferenceLineageFile>(index);
        const auto &observed = fixture.verified_lineage.at(file);
        const auto &manifest = lineage_payload(inputs->fixture, file);
        expect(manifest.byte_count == observed.byte_count &&
                   manifest.payload_sha256 == observed.payload_sha256,
               "manifest lineage identity was not copied from verified observation");
    }

    const auto &presentation = inputs->presentation;
    const std::array presentation_methods{
        std::pair{P18ReferenceMethod::reconstruction,
                  &presentation.methods.reconstruction.value},
        std::pair{P18ReferenceMethod::conditioning,
                  &presentation.methods.conditioning.value},
        std::pair{P18ReferenceMethod::impulse_response_conversion,
                  &presentation.methods.impulse_response_conversion.value},
        std::pair{P18ReferenceMethod::convolution,
                  &presentation.methods.convolution.value},
        std::pair{P18ReferenceMethod::publication,
                  &presentation.methods.publication.value},
        std::pair{P18ReferenceMethod::audition_mix,
                  &presentation.methods.audition_mix.value},
    };
    for (const auto &[selected, actual] : presentation_methods) {
        expect(*actual == p18_reference_method_identity(selected).contract_identity(),
               "manifest presentation did not retain a pinned method identity");
    }
    expect(presentation.algorithm_record.content_sha256.value ==
                   fixture.verified_lineage
                       .at(P18ReferenceLineageFile::renderer_algorithm_record)
                       .payload_sha256 &&
               presentation.assets.size() == 1 &&
               presentation.assets[0].content_sha256.value ==
                   fixture.verified_lineage
                       .at(P18ReferenceLineageFile::configured_ir_input)
                       .payload_sha256,
           "manifest presentation payload identity did not come from observed lineage");
    expect(inputs->capture.record_count == fixture.audit.frames.size(),
           "manifest capture extent did not come from the decoded audit");

    expect(content.randomness.generator ==
                   p18_reference_method_identity(P18ReferenceMethod::random_generator)
                       .contract_identity() &&
               content.randomness.derivation ==
                   p18_reference_method_identity(P18ReferenceMethod::seed_derivation)
                       .contract_identity(),
           "manifest randomness did not retain pinned method identities");
    const auto route_seeds = fixture.component_seeds.route_seeds();
    const std::array expected_seed_pairs{
        route_seeds[0].air_noise,
        route_seeds[1].air_noise,
        route_seeds[0].jitter,
        route_seeds[1].jitter,
    };
    expect(content.randomness.component_seeds.size() == expected_seed_pairs.size(),
           "manifest did not retain exactly the four executed seed pairs");
    for (std::size_t index = 0; index < expected_seed_pairs.size(); ++index) {
        expect(content.randomness.component_seeds[index].initial_state ==
                       expected_seed_pairs[index].initial_state &&
                   content.randomness.component_seeds[index].stream ==
                       expected_seed_pairs[index].stream,
               "manifest seed pair did not come from the decoded inventory");
    }

    expect(content.artifacts.size() == catalog.expected_audio.size(),
           "manifest did not contain exactly eight audio artifacts");
    for (std::size_t index = 0; index < catalog.expected_audio.size(); ++index) {
        const auto &expected = catalog.expected_audio[index];
        const auto observed = artifacts.record(expected.audio);
        expect(observed.has_value(), "exact artifact set lost a sealed record");
        const auto &manifest = content.artifacts[index];
        expect(manifest.role == observed->role &&
                   manifest.relative_path == observed->relative_path &&
                   manifest.byte_count == observed->byte_count &&
                   manifest.payload_sha256 == observed->payload_sha256,
               "manifest artifact identity was not copied from the sealed record");
        expect(observed->byte_count == expected.expected_byte_count &&
                   observed->payload_sha256 == expected.expected_sha256,
               "frozen test artifact differs from the liked reference");
    }

    const auto oracle = observe_file_identity(audition_oracle);
    const auto audition = artifacts.record(P18ReferenceAudioArtifact::master_audition);
    expect(
        audition.has_value() && audition->byte_count == oracle.byte_count &&
            audition->payload_sha256 == oracle.payload_sha256,
        "blockwise-derived audition does not match the independently observed oracle");
}

void test_unsealed_and_wrong_artifacts_fail(
    const P18LoadedReferenceFixture &fixture,
    const RendererDeterminismEnvelope &renderer_identity,
    const std::filesystem::path &temporary_root) {
    auto unsealed = require_artifact_set(
        P18ReferenceArtifactSet::create(temporary_root, "unsealed"));
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(fixture, *unsealed,
                                                                  renderer_identity));
        },
        "unsealed artifact records were accepted by manifest construction");

    auto wrong = make_wrong_artifacts(temporary_root);
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(fixture, *wrong,
                                                                  renderer_identity));
        },
        "complete wrong-byte artifact observations were accepted as the frozen set");

    require_sink_success(wrong->publish(),
                         "wrong-byte artifact transaction could not be published");
    expect(wrong->state() == P18ReferenceArtifactSetState::published,
           "published artifact transaction did not retain its lifecycle state");
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(fixture, *wrong,
                                                                  renderer_identity));
        },
        "published artifact transaction was accepted by manifest construction");
}

void test_decoded_seed_substitution_fails(
    const P18LoadedReferenceFixture &fixture, const P18ReferenceArtifactSet &artifacts,
    const RendererDeterminismEnvelope &renderer_identity) {
    auto changed_fixture = fixture;
    changed_fixture.component_seeds.air_noise[0].initial_state ^= UINT64_C(1);
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(
                changed_fixture, artifacts, renderer_identity));
        },
        "changed decoded seed was hidden by a catalog comparator");
}

void test_poisoned_artifact_set_fails(
    const P18LoadedReferenceFixture &fixture, P18ReferenceArtifactSet &artifacts,
    const RendererDeterminismEnvelope &renderer_identity) {
    const auto repeated_seal =
        artifacts.seal(P18ReferenceAudioArtifact::master_audition);
    expect(repeated_seal.has_value(),
           "repeated seal did not poison the exact artifact transaction");
    expect(artifacts.state() == P18ReferenceArtifactSetState::open &&
               artifacts.last_error().has_value(),
           "poisoned artifact transaction did not retain its open error state");
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(fixture, artifacts,
                                                                  renderer_identity));
        },
        "poisoned artifact transaction was accepted by manifest construction");
}

void test_aborted_artifact_set_fails(
    const P18LoadedReferenceFixture &fixture,
    const RendererDeterminismEnvelope &renderer_identity,
    const std::filesystem::path &temporary_root) {
    auto aborted = require_artifact_set(
        P18ReferenceArtifactSet::create(temporary_root, "aborted"));
    aborted->abort();
    expect_exception(
        [&] {
            static_cast<void>(make_p18_reference_manifest_content(fixture, *aborted,
                                                                  renderer_identity));
        },
        "aborted artifact transaction was accepted by manifest construction");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 4,
               "expected fixture root, stem root, and audition-oracle arguments");
        const std::filesystem::path fixture_root{argv[1]};
        const std::filesystem::path stems_root{argv[2]};
        const std::filesystem::path audition_oracle{argv[3]};
        const auto fixture = load_p18_reference_fixture(fixture_root);
        TemporaryDirectory temporary;
        test_scripted_renderer_identity_is_rejected(fixture, temporary.path());

        auto renderer_result = renderer_determinism_envelope();
        auto *live_identity =
            std::get_if<RendererDeterminismEnvelope>(&renderer_result);
        if (live_identity == nullptr) {
            std::cout << "P1.8 manifest-content live-identity test skipped: "
                         "the current build is not publishable\n";
            return 77;
        }
        expect(live_identity->production_observation(),
               "zero-argument renderer identity lacked production authority");

        const auto exact = make_exact_artifacts(temporary.path(), stems_root);
        test_exact_content(fixture, *exact, *live_identity, audition_oracle);
        test_decoded_seed_substitution_fails(fixture, *exact, *live_identity);
        test_unsealed_and_wrong_artifacts_fail(fixture, *live_identity,
                                               temporary.path());
        test_aborted_artifact_set_fails(fixture, *live_identity, temporary.path());
        test_poisoned_artifact_set_fails(fixture, *exact, *live_identity);
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "P1.8 reference-manifest content test failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
