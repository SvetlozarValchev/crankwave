#include "reference/p18_reference_artifact_set.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_render_session.hpp"
#include "reference/p18_reference_verification.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

[[nodiscard]] std::runtime_error sink_error(std::string_view operation,
                                            const RenderSinkError &error) {
    return std::runtime_error{std::string(operation) + ": " + error.detail_code + ": " +
                              error.message};
}

void require_success(std::string_view operation, const RenderSinkStatus &status) {
    if (status.has_value()) {
        throw sink_error(operation, *status);
    }
}

[[nodiscard]] bool valid_source_commit(std::string_view value) noexcept {
    return value.size() >= 7 && value.size() <= 64 &&
           std::ranges::all_of(value, [](unsigned char byte) {
               return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
           });
}

[[nodiscard]] std::unique_ptr<P18ReferenceArtifactSet>
create_artifact_set(const std::filesystem::path &root, std::string name) {
    auto result = P18ReferenceArtifactSet::create(root, std::move(name));
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw sink_error("could not create P1.8 artifact set", *error);
    }
    return std::move(std::get<std::unique_ptr<P18ReferenceArtifactSet>>(result));
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
    if (argc != 5) {
        throw std::invalid_argument{
            "usage: engine-sim-offline-p18-reference-render <fixture-root> "
            "<publication-root> <publication-name> <source-commit>"};
    }
    const std::filesystem::path fixture_root{argv[1]};
    const std::filesystem::path publication_root{argv[2]};
    const std::string publication_name{argv[3]};
    const std::string source_commit{argv[4]};
    if (!valid_source_commit(source_commit)) {
        throw std::invalid_argument{
            "source commit must contain 7 to 64 lowercase hexadecimal characters"};
    }

    std::error_code directory_error;
    std::filesystem::create_directories(publication_root, directory_error);
    if (directory_error) {
        throw std::runtime_error{"could not create publication root: " +
                                 directory_error.message()};
    }

    auto fixture = load_p18_reference_fixture(fixture_root);
    auto artifact_set = create_artifact_set(publication_root, publication_name);
    P18ReferenceAudioConsumers consumers{};
    const auto &catalog = p18_reference_catalog_v1();
    const auto &audio_artifacts = catalog.expected_audio;
    for (std::size_t index = 0; index < audio_artifacts.size(); ++index) {
        const auto &description = audio_artifacts[index];
        if (&require_expected_audio(catalog, description.audio) != &description) {
            throw std::logic_error{
                "P1.8 audio catalog contains a duplicate enum identity"};
        }
        consumers[index] = artifact_set->consumer(description.audio);
    }

    const auto render_started = std::chrono::steady_clock::now();
    const auto stats =
        render_p18_reference_audio(fixture.audit, fixture.component_seeds.route_seeds(),
                                   fixture.configured_ir_kernel, consumers);
    const auto render_finished = std::chrono::steady_clock::now();
    const auto render_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        render_finished - render_started);

    for (const auto &description : audio_artifacts) {
        require_success("could not seal P1.8 audio artifact",
                        artifact_set->seal(description.audio));
    }
    const auto report = make_p18_reference_verification_report(
        fixture, stats, *artifact_set, render_duration, source_commit);
    require_success(
        "could not write P1.8 verification report",
        artifact_set->write_text_report("verification.txt", report.verification_text));
    require_success(
        "could not write P1.8 listening guide",
        artifact_set->write_text_report("LISTENING.md", report.listening_markdown));
    require_success("could not publish P1.8 listening set", artifact_set->publish());

    const auto &audition =
        require_expected_audio(catalog, P18ReferenceAudioArtifact::master_audition);
    std::cout << "publication=" << artifact_set->publication_path().string() << '\n'
              << "audition="
              << (artifact_set->publication_path() /
                  std::filesystem::path{audition.expected_relative_path})
                     .string()
              << '\n'
              << "exact_reference_match="
              << (report.exact_reference_match ? "yes" : "no") << '\n'
              << "render_seconds="
              << std::chrono::duration<double>(render_duration).count() << '\n';
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
