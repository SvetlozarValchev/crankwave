#include "contract_test_support.hpp"

#include "crankwave/artifacts/directory_render_sink.hpp"
#include "crankwave/artifacts/simulation_manifest_encoder.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/stat.h>
#endif

namespace crankwave::artifacts::test {
namespace {

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class IsolatedDirectory {
  public:
    IsolatedDirectory() {
#if defined(__linux__)
        std::array name{
            '/', 't', 'm', 'p', '/', 'e', 's', 'o', '-', 'd', 'i', 'r',  '-',
            's', 'i', 'n', 'k', '-', 'X', 'X', 'X', 'X', 'X', 'X', '\0',
        };
        const auto *created = ::mkdtemp(name.data());
        if (created == nullptr) {
            throw std::runtime_error("could not create isolated sink test directory");
        }
        path_ = created;
#else
        throw std::runtime_error(
            "directory render sink tests require Linux atomic publication");
#endif
    }

    ~IsolatedDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    IsolatedDirectory(const IsolatedDirectory &) = delete;
    IsolatedDirectory &operator=(const IsolatedDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

contract::Sha256Digest nonzero_digest(std::uint8_t first) {
    contract::Sha256Digest digest;
    digest.bytes.front() = first;
    return digest;
}

contract::AudioContract audio_contract() {
    return {
        {48000, 1},
        4,
        "mono",
        "float32_le",
    };
}

contract::OutputContract output_contract() {
    contract::OutputContract result;
    result.source_matrix_id = "directory-sink-test-matrix-v1";
    result.source_matrix_sha256 = nonzero_digest(1);
    result.distribution = contract::DistributionIntent::local_evaluation;
    result.required_artifacts = {
        {
            "audio.master",
            contract::ArtifactKind::audio,
            audio_contract(),
            false,
        },
        {
            "telemetry.capture",
            contract::ArtifactKind::telemetry,
            std::nullopt,
            false,
        },
    };
    return result;
}

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const auto character : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return result;
}

std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.resize(digest.bytes.size() * 2);
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = digits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("could not read a published test file");
    }
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };
}

contract::ArtifactRecord artifact_record(const PendingArtifact &pending,
                                         std::span<const std::byte> payload) {
    return {
        pending.role,
        pending.kind,
        pending.relative_path,
        pending.audio,
        static_cast<std::uint64_t>(payload.size()),
        contract::sha256(payload),
        pending.diagnostic,
    };
}

contract::RenderManifest manifest_for(const contract::OutputContract &contract,
                                      std::vector<contract::ArtifactRecord> records) {
    contract::test::InputBuilder builder;
    auto content = contract::test::make_manifest_content(builder);
    content.schema_version = 10;
    content.output_contract = contract;
    content.artifacts = std::move(records);
    return {
        std::move(content),
        contract::ExecutionFacts{
            "directory-sink-test-run",
            "2026-07-28T12:34:56Z",
            std::chrono::nanoseconds{1},
            "linux",
            "test-cpu",
            1,
            1,
            1,
            UINT64_C(1),
        },
    };
}

std::vector<std::byte> encoded_manifest(const contract::RenderManifest &manifest) {
    auto result = encode_simulation_manifest_v10(manifest);
    const auto *encoding = std::get_if<ManifestEncoding>(&result);
    expect(encoding != nullptr, "test manifest was not wire-representable");
    return encoding->bytes;
}

void expect_error(const RenderSinkStatus &status, RenderSinkErrorKind kind,
                  std::string_view detail_code, const char *message) {
    expect(status.has_value(), message);
    expect(status->kind == kind, message);
    expect(status->detail_code == detail_code, message);
}

void declare_write_seal(DirectoryRenderSink &sink, const PendingArtifact &pending,
                        std::span<const std::byte> first,
                        std::span<const std::byte> second,
                        contract::ArtifactRecord &record) {
    expect(!sink.declare_artifact(pending).has_value(),
           "valid artifact declaration failed");
    expect(!sink.write_artifact_chunk({pending.role, 0, first}).has_value(),
           "first contiguous artifact chunk failed");
    expect(!sink.write_artifact_chunk(
                    {pending.role, static_cast<std::uint64_t>(first.size()), second})
                .has_value(),
           "second contiguous artifact chunk failed");

    std::vector<std::byte> complete(first.begin(), first.end());
    complete.insert(complete.end(), second.begin(), second.end());
    record = artifact_record(pending, complete);
    expect(!sink.seal_artifact(record).has_value(), "valid artifact seal failed");
}

void run_success_case() {
    IsolatedDirectory isolated;
    DirectoryRenderSink sink(isolated.path(), "published-render");
    const auto contract = output_contract();

    expect(sink.state() == DirectoryRenderSinkState::idle,
           "new directory sink was not idle");
    expect(!sink.begin_transaction(contract).has_value(),
           "valid directory transaction did not begin");
    expect(sink.state() == DirectoryRenderSinkState::begun,
           "successful begin did not enter begun state");
    expect(sink.staging_path().has_value() &&
               std::filesystem::is_directory(*sink.staging_path()),
           "successful begin did not create private staging");
    expect(!std::filesystem::exists(sink.publication_path()),
           "final render became visible before commit");

    const PendingArtifact audio{
        "audio.master",
        contract::ArtifactKind::audio,
        "audio/master.f32",
        audio_contract(),
        false,
    };
    const PendingArtifact telemetry{
        "telemetry.capture",
        contract::ArtifactKind::telemetry,
        "telemetry/capture.bin",
        std::nullopt,
        false,
    };
    const auto audio_payload =
        bytes("more-than-one-sha-block:"
              "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ:"
              "split-across-callbacks");
    const auto telemetry_payload = bytes("bounded-telemetry-payload");

    contract::ArtifactRecord audio_record;
    declare_write_seal(sink, audio, std::span(audio_payload).first(37),
                       std::span(audio_payload).subspan(37), audio_record);
    contract::ArtifactRecord telemetry_record;
    declare_write_seal(sink, telemetry, std::span(telemetry_payload).first(5),
                       std::span(telemetry_payload).subspan(5), telemetry_record);

    const auto manifest = manifest_for(contract, {telemetry_record, audio_record});
    const auto manifest_document = encoded_manifest(manifest);
    if (const auto status = sink.commit(manifest)) {
        throw std::runtime_error("complete directory transaction did not commit: " +
                                 status->detail_code + ": " + status->message);
    }
    expect(sink.state() == DirectoryRenderSinkState::committed,
           "successful commit did not enter committed state");
    expect(!sink.staging_path().has_value(),
           "successful commit retained a staging identity");
    expect(std::filesystem::is_directory(sink.publication_path()),
           "successful commit did not publish the final directory");
    expect(read_file(sink.publication_path() / audio.relative_path) ==
               std::string(reinterpret_cast<const char *>(audio_payload.data()),
                           audio_payload.size()),
           "published audio bytes differ from streamed bytes");
    expect(read_file(sink.publication_path() / telemetry.relative_path) ==
               std::string(reinterpret_cast<const char *>(telemetry_payload.data()),
                           telemetry_payload.size()),
           "published telemetry bytes differ from streamed bytes");
    expect(read_file(sink.publication_path() /
                     std::string{kSimulationManifestRelativePathV10}) ==
               std::string(reinterpret_cast<const char *>(manifest_document.data()),
                           manifest_document.size()),
           "published manifest differs from the sole simulation-v10 encoder output");
    const auto manifest_digest = contract::sha256(manifest_document);
    expect(read_file(sink.publication_path() /
                     (std::string{kSimulationManifestRelativePathV10} + ".sha256")) ==
               digest_hex(manifest_digest) + "\n",
           "published manifest digest sidecar is incorrect");
    expect(sink.manifest_payload_sha256() == std::optional{manifest_digest},
           "sink did not retain the published manifest digest");
    sink.abort();
    expect(std::filesystem::is_directory(sink.publication_path()),
           "abort after commit removed published output");
}

void run_abort_and_destructor_cleanup_cases() {
    IsolatedDirectory isolated;
    const auto contract = output_contract();
    {
        DirectoryRenderSink sink(isolated.path(), "aborted-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "abort test begin failed");
        const auto stage = *sink.staging_path();
        const PendingArtifact audio{
            "audio.master",
            contract::ArtifactKind::audio,
            "partial/audio.bin",
            audio_contract(),
            false,
        };
        const auto payload = bytes("partial");
        expect(!sink.declare_artifact(audio).has_value(),
               "abort test declaration failed");
        expect(!sink.write_artifact_chunk({audio.role, 0, payload}).has_value(),
               "abort test write failed");
        sink.abort();
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "abort did not enter aborted state");
        expect(!std::filesystem::exists(stage), "abort retained private staging");
        expect(!std::filesystem::exists(sink.publication_path()),
               "abort exposed a final render");
        sink.abort();
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "repeated abort was not idempotent");
    }
    {
        std::filesystem::path stage;
        {
            DirectoryRenderSink sink(isolated.path(), "destructor-abort-render");
            expect(!sink.begin_transaction(contract).has_value(),
                   "destructor cleanup begin failed");
            stage = *sink.staging_path();
        }
        expect(!std::filesystem::exists(stage),
               "sink destruction retained an unfinished staging directory");
        expect(!std::filesystem::exists(isolated.path() / "destructor-abort-render"),
               "sink destruction published an unfinished render");
    }
}

void run_protocol_and_confinement_cases() {
    const auto contract = output_contract();
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "../invalid");
        expect_error(
            sink.begin_transaction(contract), RenderSinkErrorKind::protocol_violation,
            "publication-name-invalid", "invalid publication name did not fail closed");
        expect(sink.state() == DirectoryRenderSinkState::idle,
               "failed begin changed the idle state");
        expect(std::filesystem::is_empty(isolated.path()),
               "failed begin created filesystem staging");
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "traversal-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "traversal test begin failed");
        const PendingArtifact traversal{
            "audio.master",
            contract::ArtifactKind::audio,
            "../escaped.bin",
            audio_contract(),
            false,
        };
        expect_error(
            sink.declare_artifact(traversal), RenderSinkErrorKind::protocol_violation,
            "artifact-declaration-invalid", "parent traversal path was accepted");
        expect(!std::filesystem::exists(isolated.path().parent_path() / "escaped.bin"),
               "parent traversal created an escaped artifact");
        expect_error(sink.declare_artifact(traversal),
                     RenderSinkErrorKind::protocol_violation,
                     "sink-transaction-poisoned",
                     "failed declaration did not poison transaction");
        sink.abort();
        expect(std::filesystem::is_empty(isolated.path()),
               "aborted traversal test retained staging");
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "symlink-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "symlink test begin failed");
        const auto outside = isolated.path() / "outside";
        std::filesystem::create_directory(outside);
        std::filesystem::create_directory_symlink(outside,
                                                  *sink.staging_path() / "redirect");
        const PendingArtifact redirected{
            "audio.master",
            contract::ArtifactKind::audio,
            "redirect/escaped.bin",
            audio_contract(),
            false,
        };
        expect_error(
            sink.declare_artifact(redirected), RenderSinkErrorKind::publication_failure,
            "artifact-parent-open-failed", "symbolic-link parent was followed");
        expect(!std::filesystem::exists(outside / "escaped.bin"),
               "symbolic-link parent escaped staging confinement");
        sink.abort();
        expect(std::filesystem::is_empty(outside),
               "abort followed or modified an injected symbolic-link target");
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "offset-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "offset test begin failed");
        const PendingArtifact audio{
            "audio.master", contract::ArtifactKind::audio,
            "audio.bin",    audio_contract(),
            false,
        };
        expect(!sink.declare_artifact(audio).has_value(),
               "offset test declaration failed");
        const auto payload = bytes("chunk");
        expect_error(sink.write_artifact_chunk({audio.role, 1, payload}),
                     RenderSinkErrorKind::protocol_violation,
                     "artifact-offset-noncontiguous",
                     "noncontiguous first write was accepted");
        sink.abort();
    }
}

void run_portable_path_identity_cases() {
    const auto contract = output_contract();
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "escaped-role-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "escaped role path test begin failed");
        const PendingArtifact escaped{
            "audio.master",
            contract::ArtifactKind::audio,
            "audio/route%2fselected.wav",
            audio_contract(),
            false,
        };
        expect(!sink.declare_artifact(escaped).has_value(),
               "lowercase percent-escaped role path was rejected");
        sink.abort();
    }
    for (const std::string_view invalid_path : {
             "audio/route%selected.wav",
             "audio/route%2Fselected.wav",
         }) {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "invalid-escape-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "invalid escaped path test begin failed");
        const PendingArtifact escaped{
            "audio.master",
            contract::ArtifactKind::audio,
            std::string{invalid_path},
            audio_contract(),
            false,
        };
        expect_error(
            sink.declare_artifact(escaped), RenderSinkErrorKind::protocol_violation,
            "artifact-declaration-invalid", "noncanonical percent escape was accepted");
        sink.abort();
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "manifest-path-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "manifest path reservation test begin failed");
        const PendingArtifact collision{
            "audio.master",
            contract::ArtifactKind::audio,
            std::string{kSimulationManifestRelativePathV10},
            audio_contract(),
            false,
        };
        expect_error(sink.declare_artifact(collision),
                     RenderSinkErrorKind::protocol_violation, "artifact-path-duplicate",
                     "schema-owned manifest path was accepted as an artifact");
        sink.abort();
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "reserved-path-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "reserved path test begin failed");
        const PendingArtifact invalid{
            "audio.master", contract::ArtifactKind::audio,
            "CON/file.bin", audio_contract(),
            false,
        };
        expect_error(sink.declare_artifact(invalid),
                     RenderSinkErrorKind::protocol_violation,
                     "artifact-declaration-invalid",
                     "Windows-reserved path component was accepted as portable");
        sink.abort();
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "case-alias-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "case alias test begin failed");
        const PendingArtifact audio{
            "audio.master", contract::ArtifactKind::audio,
            "Case/x.bin",   audio_contract(),
            false,
        };
        const PendingArtifact telemetry{
            "telemetry.capture",
            contract::ArtifactKind::telemetry,
            "case/y.bin",
            std::nullopt,
            false,
        };
        expect(!sink.declare_artifact(audio).has_value(),
               "case alias test first declaration failed");
        expect_error(sink.declare_artifact(telemetry),
                     RenderSinkErrorKind::protocol_violation, "artifact-path-duplicate",
                     "case-alias parent components were accepted");
        sink.abort();
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "prefix-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "path prefix test begin failed");
        const PendingArtifact audio{
            "audio.master", contract::ArtifactKind::audio, "tree.bin", audio_contract(),
            false,
        };
        const PendingArtifact telemetry{
            "telemetry.capture",
            contract::ArtifactKind::telemetry,
            "tree.bin/child.bin",
            std::nullopt,
            false,
        };
        expect(!sink.declare_artifact(audio).has_value(),
               "path prefix test first declaration failed");
        expect_error(sink.declare_artifact(telemetry),
                     RenderSinkErrorKind::protocol_violation, "artifact-path-duplicate",
                     "file/directory prefix conflict was accepted");
        sink.abort();
    }
}

void run_seal_and_completeness_cases() {
    const auto contract = output_contract();
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "digest-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "digest test begin failed");
        const PendingArtifact audio{
            "audio.master", contract::ArtifactKind::audio,
            "audio.bin",    audio_contract(),
            false,
        };
        const auto payload = bytes("hash-me-across-a-block-boundary:"
                                   "abcdefghijklmnopqrstuvwxyz0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        expect(!sink.declare_artifact(audio).has_value(),
               "digest test declaration failed");
        expect(!sink.write_artifact_chunk({audio.role, 0, std::span(payload).first(63)})
                    .has_value(),
               "digest test first chunk failed");
        expect(
            !sink.write_artifact_chunk({audio.role, 63, std::span(payload).subspan(63)})
                 .has_value(),
            "digest test second chunk failed");
        auto record = artifact_record(audio, payload);
        record.payload_sha256.bytes.front() ^= 0xffU;
        expect_error(
            sink.seal_artifact(record), RenderSinkErrorKind::protocol_violation,
            "artifact-digest-mismatch", "incorrect artifact digest was accepted");
        sink.abort();
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "incomplete-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "completeness test begin failed");
        const PendingArtifact audio{
            "audio.master", contract::ArtifactKind::audio,
            "audio.bin",    audio_contract(),
            false,
        };
        const auto payload = bytes("complete-audio-only");
        contract::ArtifactRecord audio_record;
        declare_write_seal(sink, audio, payload, {}, audio_record);
        const auto manifest = manifest_for(contract, {audio_record});
        expect_error(sink.commit(manifest), RenderSinkErrorKind::protocol_violation,
                     "required-artifact-incomplete",
                     "commit accepted an incomplete required artifact set");
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "failed commit did not become terminally aborted");
        expect(std::filesystem::is_empty(isolated.path()),
               "failed incomplete commit retained staging or final output");
    }
    {
        IsolatedDirectory isolated;
        DirectoryRenderSink sink(isolated.path(), "wrong-manifest-version-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "manifest-version test begin failed");
        const PendingArtifact audio{
            "audio.master", contract::ArtifactKind::audio,
            "audio.bin",    audio_contract(),
            false,
        };
        const PendingArtifact telemetry{
            "telemetry.capture",
            contract::ArtifactKind::telemetry,
            "telemetry.bin",
            std::nullopt,
            false,
        };
        const auto audio_payload = bytes("audio");
        const auto telemetry_payload = bytes("telemetry");
        contract::ArtifactRecord audio_record;
        contract::ArtifactRecord telemetry_record;
        declare_write_seal(sink, audio, audio_payload, {}, audio_record);
        declare_write_seal(sink, telemetry, telemetry_payload, {}, telemetry_record);

        auto manifest = manifest_for(contract, {audio_record, telemetry_record});
        manifest.content.schema_version = 4;
        expect_error(sink.commit(manifest), RenderSinkErrorKind::protocol_violation,
                     "simulation-manifest-wire-unrepresentable",
                     "directory sink accepted a non-v10 manifest");
        expect(sink.state() == DirectoryRenderSinkState::aborted &&
                   std::filesystem::is_empty(isolated.path()),
               "manifest-version failure retained staging or final output");
    }
}

void run_atomic_no_overwrite_case() {
    IsolatedDirectory isolated;
    const auto contract = output_contract();
    DirectoryRenderSink sink(isolated.path(), "existing-render");
    expect(!sink.begin_transaction(contract).has_value(),
           "no-overwrite test begin failed");

    const PendingArtifact audio{
        "audio.master", contract::ArtifactKind::audio, "audio.bin", audio_contract(),
        false,
    };
    const PendingArtifact telemetry{
        "telemetry.capture",
        contract::ArtifactKind::telemetry,
        "telemetry.bin",
        std::nullopt,
        false,
    };
    const auto audio_payload = bytes("audio");
    const auto telemetry_payload = bytes("telemetry");
    contract::ArtifactRecord audio_record;
    contract::ArtifactRecord telemetry_record;
    declare_write_seal(sink, audio, audio_payload, {}, audio_record);
    declare_write_seal(sink, telemetry, telemetry_payload, {}, telemetry_record);

    const auto destination = sink.publication_path();
    std::filesystem::create_directory(destination);
    {
        std::ofstream sentinel(destination / "sentinel.txt", std::ios::binary);
        sentinel << "preserve";
    }
    const auto manifest = manifest_for(contract, {audio_record, telemetry_record});
    expect_error(sink.commit(manifest), RenderSinkErrorKind::publication_failure,
                 "publication-destination-exists",
                 "atomic commit overwrote an existing destination");
    expect(sink.state() == DirectoryRenderSinkState::aborted,
           "no-overwrite commit failure did not become aborted");
    expect(read_file(destination / "sentinel.txt") == "preserve",
           "atomic commit modified an existing destination");
    expect(std::ranges::distance(
               std::filesystem::directory_iterator(isolated.path())) == 1,
           "failed no-overwrite commit retained private staging");
}

void run_post_seal_tamper_cases() {
    const PendingArtifact audio{
        "audio.master", contract::ArtifactKind::audio, "audio.bin", audio_contract(),
        false,
    };
    const auto original = bytes("original-bytes");
    const auto replacement = bytes("tampered-bytes");
    static_assert(std::string_view("original-bytes").size() ==
                  std::string_view("tampered-bytes").size());

    {
        IsolatedDirectory isolated;
        auto contract = output_contract();
        contract.required_artifacts.resize(1);
        DirectoryRenderSink sink(isolated.path(), "tampered-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "post-seal tamper test begin failed");
        contract::ArtifactRecord record;
        declare_write_seal(sink, audio, original, {}, record);

        {
            std::fstream staged(*sink.staging_path() / audio.relative_path,
                                std::ios::in | std::ios::out | std::ios::binary);
            expect(static_cast<bool>(staged),
                   "could not open sealed artifact for tamper test");
            staged.write(reinterpret_cast<const char *>(replacement.data()),
                         static_cast<std::streamsize>(replacement.size()));
        }
        const auto manifest = manifest_for(contract, {record});
        expect_error(sink.commit(manifest), RenderSinkErrorKind::publication_failure,
                     "artifact-final-digest-mismatch",
                     "same-size post-seal payload rewrite passed final verification");
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "tampered commit did not become aborted");
        expect(std::filesystem::is_empty(isolated.path()),
               "tampered commit retained staging or published output");
    }

#if defined(__linux__)
    {
        IsolatedDirectory isolated;
        auto contract = output_contract();
        contract.required_artifacts.resize(1);
        DirectoryRenderSink sink(isolated.path(), "fifo-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "FIFO replacement test begin failed");
        contract::ArtifactRecord record;
        declare_write_seal(sink, audio, original, {}, record);
        const auto artifact_path = *sink.staging_path() / audio.relative_path;
        expect(std::filesystem::remove(artifact_path),
               "could not remove sealed artifact for FIFO test");
        expect(::mkfifo(artifact_path.c_str(), 0600) == 0,
               "could not create FIFO replacement for sink test");

        const auto manifest = manifest_for(contract, {record});
        expect_error(sink.commit(manifest), RenderSinkErrorKind::publication_failure,
                     "staging-tree-sync-failed",
                     "non-regular post-seal replacement passed staging verification");
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "FIFO replacement commit did not become aborted");
        expect(std::filesystem::is_empty(isolated.path()),
               "FIFO replacement retained staging or published output");
    }
#endif
}

void run_staging_tree_integrity_cases() {
    const PendingArtifact audio{
        "audio.master", contract::ArtifactKind::audio, "audio.bin", audio_contract(),
        false,
    };
    const auto payload = bytes("immutable-payload");

    {
        IsolatedDirectory isolated;
        auto contract = output_contract();
        contract.required_artifacts.resize(1);
        DirectoryRenderSink sink(isolated.path(), "injected-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "staging injection test begin failed");
        contract::ArtifactRecord record;
        declare_write_seal(sink, audio, payload, {}, record);
        {
            std::ofstream injected(*sink.staging_path() / "injected.bin",
                                   std::ios::binary);
            injected << "undeclared";
        }
        const auto manifest = manifest_for(contract, {record});
        expect_error(sink.commit(manifest), RenderSinkErrorKind::publication_failure,
                     "staging-inventory-limit-exceeded",
                     "commit published an undeclared regular file from staging");
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "injected tree commit did not become aborted");
        expect(std::filesystem::is_empty(isolated.path()),
               "injected tree failure retained owned staging");
    }

    {
        IsolatedDirectory isolated;
        auto contract = output_contract();
        contract.required_artifacts.resize(1);
        DirectoryRenderSink sink(isolated.path(), "stage-replacement-render");
        expect(!sink.begin_transaction(contract).has_value(),
               "stage replacement test begin failed");
        contract::ArtifactRecord record;
        declare_write_seal(sink, audio, payload, {}, record);

        const auto named_stage = *sink.staging_path();
        const auto moved_stage = isolated.path() / "moved-owned-stage";
        std::filesystem::rename(named_stage, moved_stage);
        std::filesystem::create_directory(named_stage);
        {
            std::ofstream sentinel(named_stage / "sentinel.txt", std::ios::binary);
            sentinel << "unowned-replacement";
        }

        const auto manifest = manifest_for(contract, {record});
        expect_error(sink.commit(manifest), RenderSinkErrorKind::publication_failure,
                     "staging-directory-identity-mismatch",
                     "commit published a replacement directory under the staging name");
        expect(sink.state() == DirectoryRenderSinkState::aborted,
               "stage identity failure did not become aborted");
        expect(!std::filesystem::exists(moved_stage),
               "stage identity failure did not clean the owned directory");
        expect(read_file(named_stage / "sentinel.txt") == "unowned-replacement",
               "stage cleanup deleted an unowned replacement directory");
        expect(!std::filesystem::exists(sink.publication_path()),
               "stage identity failure exposed a final render");
    }
}

} // namespace
} // namespace crankwave::artifacts::test

int main() {
    using namespace crankwave::artifacts::test;
    run_success_case();
    run_abort_and_destructor_cleanup_cases();
    run_protocol_and_confinement_cases();
    run_portable_path_identity_cases();
    run_seal_and_completeness_cases();
    run_atomic_no_overwrite_case();
    run_post_seal_tamper_cases();
    run_staging_tree_integrity_cases();
}
