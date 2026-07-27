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

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

[[nodiscard]] std::runtime_error
determinism_error(const determinism::RendererDeterminismEnvelopeResult &result) {
    if (const auto *error =
            std::get_if<determinism::RendererNumericEnvironmentError>(&result)) {
        return std::runtime_error{"renderer numeric environment rejected at " +
                                  std::string(error->component) + ": " +
                                  std::string(error->message)};
    }
    if (const auto *error =
            std::get_if<determinism::RendererSourceStampError>(&result)) {
        return std::runtime_error{"renderer source stamp rejected: " + error->message};
    }
    if (const auto *error = std::get_if<determinism::LoadedRuntimeError>(&result)) {
        return std::runtime_error{"renderer runtime provider rejected at " +
                                  error->component + "/" + error->symbol + ": " +
                                  error->message};
    }
    if (std::holds_alternative<determinism::RendererThreadStateChanged>(result)) {
        return std::runtime_error{
            "renderer numeric environment changed while identity was observed"};
    }
    return std::runtime_error{"renderer identity failed without a typed rejection"};
}

[[nodiscard]] determinism::RendererDeterminismEnvelope require_renderer_identity() {
    auto result = determinism::renderer_determinism_envelope();
    if (auto *identity =
            std::get_if<determinism::RendererDeterminismEnvelope>(&result)) {
        return std::move(*identity);
    }
    throw determinism_error(result);
}

[[nodiscard]] const P18ExpectedAudioComparator &
require_expected_audio(const P18ReferenceCatalogV1 &catalog,
                       P18ReferenceAudioArtifact artifact) {
    const auto *expected = catalog.find_expected_audio(artifact);
    if (expected == nullptr) {
        throw std::logic_error{
            "P1.8 audio catalog is not exhaustive canonical enum order"};
    }
    return *expected;
}

int run(int argc, char **argv) {
    if (argc != 4) {
        throw std::invalid_argument{
            "usage: engine-sim-offline-p18-reference-render <fixture-root> "
            "<publication-root> <publication-name>"};
    }
    const auto command_started = std::chrono::steady_clock::now();
    const auto renderer_identity = require_renderer_identity();
    const std::filesystem::path fixture_root{argv[1]};
    const std::filesystem::path publication_root{argv[2]};
    const std::string publication_name{argv[3]};

    std::error_code directory_error;
    std::filesystem::create_directories(publication_root, directory_error);
    if (directory_error) {
        throw std::runtime_error{"could not create publication root: " +
                                 directory_error.message()};
    }

    auto fixture = load_p18_reference_fixture(fixture_root);
    auto plan = make_p18_reference_presentation_session_plan();
    artifacts::DirectoryRenderSink sink{
        publication_root,
        publication_name,
        std::string{artifacts::kReferenceManifestRelativePathV2},
        artifacts::encode_reference_manifest_v2,
    };

    const auto render_started = std::chrono::steady_clock::now();
    P18PresentationSession session{
        sink,
        std::move(plan),
        p18_reference_presentation_seeds(fixture.component_seeds),
        fixture.configured_ir_kernel,
    };
    replay_p18_reference_audit(fixture.audit, session);
    auto evidence = session.finish();
    const auto render_finished = std::chrono::steady_clock::now();

    const auto manifest_content =
        make_p18_reference_manifest_content(fixture, evidence, renderer_identity);
    const auto completed_manifest =
        complete_p18_reference_manifest(manifest_content, evidence.execution());
    const auto &source_matrix = contract::bmw_m52b28_reference_source_matrix_v1();
    session.commit(evidence, completed_manifest.manifest(),
                   completed_manifest.provenance().ledger(), source_matrix);
    const auto command_finished = std::chrono::steady_clock::now();

    const auto expected_manifest_sha =
        contract::sha256(completed_manifest.canonical_bytes());
    if (sink.manifest_payload_sha256() !=
        std::optional<contract::Sha256Digest>{expected_manifest_sha}) {
        throw std::logic_error{
            "published manifest differs from the validated canonical bytes"};
    }

    const auto &audition = require_expected_audio(
        p18_reference_catalog_v1(), P18ReferenceAudioArtifact::master_audition);
    const auto render_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        render_finished - render_started);
    const auto command_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        command_finished - command_started);

    std::cout << "publication=" << sink.publication_path().string() << '\n'
              << "audition="
              << (sink.publication_path() /
                  std::filesystem::path{audition.expected_relative_path})
                     .string()
              << '\n'
              << "exact_reference_match=yes\n"
              << "render_seconds="
              << std::chrono::duration<double>(render_duration).count() << '\n'
              << "execution_seconds="
              << std::chrono::duration<double>(
                     evidence.execution().facts().wall_elapsed)
                     .count()
              << '\n'
              << "total_command_seconds="
              << std::chrono::duration<double>(command_duration).count() << '\n'
              << "manifest_bytes=" << completed_manifest.canonical_bytes().size()
              << '\n'
              << "published_file_count=10\n";
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "P1.8 reference render failed: " << error.what() << '\n';
        return 1;
    }
}
