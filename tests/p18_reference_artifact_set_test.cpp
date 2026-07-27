#include "reference/p18_reference_artifact_set.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include <unistd.h>

namespace {

using engine_sim_offline::RenderSinkError;
using engine_sim_offline::reference::P18ReferenceArtifactSet;
using engine_sim_offline::reference::P18ReferenceAudioArtifact;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        constexpr std::string_view pattern =
            "/tmp/engine-sim-offline-p18-artifacts-XXXXXX";
        std::array<char, 64> writable{};
        expect(pattern.size() + 1 <= writable.size(),
               "temporary-directory template overflow");
        std::copy(pattern.begin(), pattern.end(), writable.begin());
        const auto *created = ::mkdtemp(writable.data());
        expect(created != nullptr, "could not create temporary test directory");
        path_ = created;
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

std::unique_ptr<P18ReferenceArtifactSet>
require_set(P18ReferenceArtifactSet::CreateResult result) {
    if (auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error(error->detail_code + ": " + error->message);
    }
    return std::move(std::get<std::unique_ptr<P18ReferenceArtifactSet>>(result));
}

void test_fixed_inventory_and_identity_cleanup() {
    TemporaryDirectory temporary;
    const auto &descriptions =
        engine_sim_offline::reference::p18_reference_catalog_v1().expected_audio;
    expect(descriptions.size() == 8, "P1.8 audio inventory size changed");
    expect(descriptions.front().expected_relative_path ==
               "audio/exhaust.reference.0.dry.wav",
           "first P1.8 audio path changed");
    expect(descriptions.back().expected_relative_path ==
               "audio/master.reference.audition.wav",
           "audition P1.8 audio path changed");

    auto artifact_set =
        require_set(P18ReferenceArtifactSet::create(temporary.path(), "candidate"));
    const auto staging = artifact_set->staging_path();
    expect(staging.has_value(), "open set did not expose its staging path");
    for (const auto &description : descriptions) {
        const auto file = *staging / description.expected_relative_path;
        expect(std::filesystem::is_regular_file(file),
               "fixed P1.8 audio file was not created eagerly");
        expect(std::filesystem::file_size(file) == 0,
               "new P1.8 audio file was not empty");
    }

    const std::array payload{std::byte{0x42}};
    auto consumer = artifact_set->consumer(P18ReferenceAudioArtifact::exhaust_0_dry);
    expect(!consumer(1, payload), "nonzero initial byte offset was accepted");
    expect(artifact_set->last_error().has_value(),
           "rejected callback did not retain its error");
    artifact_set.reset();

    expect(!std::filesystem::exists(*staging),
           "abandoned P1.8 staging inode was not removed");
    expect(!std::filesystem::exists(temporary.path() / "candidate"),
           "abandoned P1.8 output became visible");
}

void test_destination_and_name_are_no_replace() {
    TemporaryDirectory temporary;
    std::filesystem::create_directory(temporary.path() / "existing");

    auto existing = P18ReferenceArtifactSet::create(temporary.path(), "existing");
    expect(std::holds_alternative<RenderSinkError>(existing),
           "existing publication destination was accepted");
    expect(std::get<RenderSinkError>(existing).detail_code ==
               "p18-publication-destination-exists",
           "existing destination returned the wrong failure");

    auto invalid = P18ReferenceArtifactSet::create(temporary.path(), "../escape");
    expect(std::holds_alternative<RenderSinkError>(invalid),
           "non-portable publication name was accepted");
    expect(std::get<RenderSinkError>(invalid).detail_code ==
               "p18-publication-name-invalid",
           "invalid publication name returned the wrong failure");
}

// This exercises the fixed 89 MB contract and final re-read without burdening the
// default focused test. Run it explicitly while changing publication mechanics.
void test_complete_publication() {
    TemporaryDirectory temporary;
    auto artifact_set =
        require_set(P18ReferenceArtifactSet::create(temporary.path(), "complete"));
    std::array<std::byte, 64U * 1024U> zeros{};
    for (const auto &description :
         engine_sim_offline::reference::p18_reference_catalog_v1().expected_audio) {
        auto consume = artifact_set->consumer(description.audio);
        std::uint64_t offset = 0;
        while (offset < description.expected_byte_count) {
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                zeros.size(), description.expected_byte_count - offset));
            expect(consume(offset, std::span<const std::byte>(zeros).first(count)),
                   "complete publication rejected a bounded contiguous chunk");
            offset += count;
        }
        expect(!artifact_set->seal(description.audio).has_value(),
               "complete publication rejected an exact-size artifact");
        const auto record = artifact_set->record(description.audio);
        expect(record.has_value() &&
                   record->byte_count == description.expected_byte_count &&
                   !record->payload_sha256.is_zero(),
               "sealed P1.8 record did not retain size and actual digest");
    }
    expect(
        !artifact_set->write_text_report("verification.txt", "synthetic\n").has_value(),
        "complete publication rejected a bounded text report");
    const auto destination = artifact_set->publication_path();
    expect(!artifact_set->publish().has_value(),
           "complete exact-size P1.8 set did not publish");
    expect(std::filesystem::is_directory(destination),
           "published P1.8 directory is missing");
    expect(std::filesystem::file_size(destination / "verification.txt") == 10,
           "published P1.8 report size changed");
}

} // namespace

int main() {
    try {
        test_fixed_inventory_and_identity_cleanup();
        test_destination_and_name_are_no_replace();
        if (std::getenv("ENGINE_SIM_OFFLINE_P18_FULL_ARTIFACT_SET_TEST") != nullptr) {
            test_complete_publication();
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
