#include "determinism/renderer_determinism_envelope.hpp"
#include "engine_sim_offline/artifacts/directory_render_sink.hpp"
#include "engine_sim_offline/artifacts/reference_manifest_encoder.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_manifest_completion.hpp"
#include "reference/p18_reference_manifest_content.hpp"
#include "reference/p18_reference_render_session.hpp"
#include "reference/p18_reference_session_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

#if defined(__linux__)

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        char pattern[] = "/tmp/engine-sim-offline-p18-transaction-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        expect(created != nullptr, "could not create isolated temporary directory");
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

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    expect(static_cast<bool>(stream), "could not open published file");
    std::string bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
    expect(!stream.bad(), "could not read published file");
    return bytes;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '\0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = digits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

void expect_exact_evidence(const P18SealedPresentationEvidence &evidence) {
    const auto &catalog = p18_reference_catalog_v1();
    const auto &records = evidence.artifacts();
    expect(records.size() == 8U, "session did not seal eight artifact records");
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto &actual = records[index];
        const auto &expected = catalog.expected_audio[index];
        expect(expected.audio == static_cast<P18ReferenceAudioArtifact>(index),
               "audio catalog order changed");
        expect(actual.role == expected.expected_role &&
                   actual.relative_path == expected.expected_relative_path &&
                   actual.byte_count == expected.expected_byte_count &&
                   actual.payload_sha256 == expected.expected_sha256 &&
                   actual.diagnostic == expected.expected_diagnostic,
               "sealed artifact differs from the frozen catalog");
    }

    const auto &stats = evidence.stats();
    expect(stats.input_frame_count == UINT64_C(170000) &&
               stats.processed_block_count == UINT64_C(850) &&
               stats.warmup_block_count == UINT64_C(100) &&
               stats.published_block_count == UINT64_C(750) &&
               stats.processed_source_frame_count == UINT64_C(3264000) &&
               stats.warmup_source_frame_count == UINT64_C(384000) &&
               stats.published_source_frame_count == UINT64_C(2880000),
           "presentation session count statistics changed");
}

void expect_complete_manifest(const P18CompletedReferenceManifest &completed,
                              const P18SealedPresentationEvidence &evidence) {
    const auto &manifest = completed.manifest();
    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();
    expect(manifest.content.routes.size() ==
                   source_matrix.required_source_routes.size() &&
               !manifest.content.routes.empty(),
           "completed manifest lacks contracted routing evidence");
    expect(manifest.content.output_buses.size() ==
                   source_matrix.required_output_buses.size() &&
               !manifest.content.output_buses.empty(),
           "completed manifest lacks contracted output-bus evidence");
    expect(manifest.execution.has_value() &&
               *manifest.execution == evidence.execution().facts(),
           "completed manifest lacks the session execution evidence");
    expect(contract::validate(manifest, completed.provenance().ledger(), source_matrix)
               .ok(),
           "completed manifest failed semantic validation");
}

void expect_exact_publication(const std::filesystem::path &publication,
                              const P18CompletedReferenceManifest &completed) {
    const auto &catalog = p18_reference_catalog_v1();
    std::set<std::string> expected_files;
    for (const auto &audio : catalog.expected_audio) {
        expected_files.emplace(audio.expected_relative_path);
    }
    const std::string manifest_path{artifacts::kReferenceManifestRelativePathV2};
    expected_files.emplace(manifest_path);
    expected_files.emplace(manifest_path + ".sha256");

    std::set<std::string> observed_files;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator{publication}) {
        const auto status = entry.symlink_status();
        if (std::filesystem::is_directory(status)) {
            continue;
        }
        expect(std::filesystem::is_regular_file(status),
               "publication contains a non-regular entry");
        const auto relative =
            std::filesystem::relative(entry.path(), publication).generic_string();
        expect(observed_files.emplace(relative).second,
               "publication contains a duplicate file identity");
    }
    expect(observed_files.size() == 10U && observed_files == expected_files,
           "publication inventory is not exactly eight WAVs and two metadata files");

    const auto published_manifest = read_file(publication / manifest_path);
    const auto canonical = completed.canonical_bytes();
    expect(
        published_manifest.size() == canonical.size() &&
            std::equal(canonical.begin(), canonical.end(),
                       reinterpret_cast<const std::byte *>(published_manifest.data())),
        "published manifest differs from the retained canonical bytes");

    const auto expected_sidecar = digest_hex(contract::sha256(canonical)) + "\n";
    expect(read_file(publication / (manifest_path + ".sha256")) == expected_sidecar,
           "published manifest sidecar is not canonical SHA-256 plus LF");
}

int run(const std::filesystem::path &fixture_root,
        const determinism::RendererDeterminismEnvelope &renderer_identity) {
    TemporaryDirectory temporary;
    auto fixture = load_p18_reference_fixture(fixture_root);
    auto plan = make_p18_reference_presentation_session_plan();
    artifacts::DirectoryRenderSink sink{
        temporary.path(),
        "publication",
        std::string{artifacts::kReferenceManifestRelativePathV2},
        artifacts::encode_reference_manifest_v2,
    };

    P18PresentationSession session{
        sink,
        std::move(plan),
        p18_reference_presentation_seeds(fixture.component_seeds),
        fixture.configured_ir_kernel,
    };
    replay_p18_reference_audit(fixture.audit, session);
    auto evidence = session.finish();
    expect_exact_evidence(evidence);

    const auto content =
        make_p18_reference_manifest_content(fixture, evidence, renderer_identity);
    const auto completed =
        complete_p18_reference_manifest(content, evidence.execution());
    expect_complete_manifest(completed, evidence);

    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();
    session.commit(evidence, completed.manifest(), completed.provenance().ledger(),
                   source_matrix);
    expect(session.state() == P18PresentationSessionState::committed,
           "successful reference transaction did not enter committed state");
    expect_exact_publication(sink.publication_path(), completed);
    return EXIT_SUCCESS;
}

#endif

} // namespace

int main(int argc, char **argv) {
#if !defined(__linux__)
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::cout << "P1.8 reference transaction test skipped: Linux is required\n";
    return 77;
#else
    try {
        expect(argc == 2, "expected fixture-root argument");
        auto renderer_result = determinism::renderer_determinism_envelope();
        auto *renderer_identity =
            std::get_if<determinism::RendererDeterminismEnvelope>(&renderer_result);
        if (renderer_identity == nullptr ||
            !renderer_identity->production_observation()) {
            std::cout << "P1.8 reference transaction test skipped: "
                         "the current build is not publishable\n";
            return 77;
        }
        return run(std::filesystem::path{argv[1]}, *renderer_identity);
    } catch (const std::exception &error) {
        std::cerr << "P1.8 reference transaction test failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
#endif
}
