#include "reference/p18_reference_verification.hpp"

#include "artifacts/directory_render_sink_support.hpp"

#include <array>
#include <bit>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace engine_sim_offline::reference {
namespace {

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    return artifacts::detail::digest_hex(digest);
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

[[nodiscard]] const P18ExpectedMasteringComparator &
require_expected_mastering(const P18ReferenceCatalogV1 &catalog,
                           P18ReferenceMasteringPayload payload) {
    const auto *expected = catalog.find_expected_mastering(payload);
    if (expected == nullptr) {
        throw std::logic_error{
            "P1.8 mastering catalog is not exhaustive canonical enum order"};
    }
    return *expected;
}

[[nodiscard]] std::string_view
mastering_label(P18ReferenceMasteringPayload payload) noexcept {
    switch (payload) {
    case P18ReferenceMasteringPayload::raw_float32:
        return "raw_float32_payload";
    case P18ReferenceMasteringPayload::monitoring_float32:
        return "monitoring_float32_payload";
    case P18ReferenceMasteringPayload::faded_float32:
        return "faded_float32_payload";
    case P18ReferenceMasteringPayload::s32le:
        return "s32le_payload";
    case P18ReferenceMasteringPayload::pcm24le:
        return "pcm24le_payload";
    }
    return "unknown_mastering_payload";
}

[[nodiscard]] const char *compiler_identity() noexcept {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown C++ compiler";
#endif
}

[[nodiscard]] double seconds(std::chrono::nanoseconds duration) noexcept {
    return std::chrono::duration<double>(duration).count();
}

void append_listening_link(std::ostringstream &output, std::string_view label,
                           const P18ExpectedAudioComparator &audio) {
    const auto separator = audio.expected_relative_path.rfind('/');
    const auto filename = separator == std::string_view::npos
                              ? audio.expected_relative_path
                              : audio.expected_relative_path.substr(separator + 1U);
    output << "- " << label << ": [" << filename << "](" << audio.expected_relative_path
           << ")\n";
}

} // namespace

P18ReferenceVerificationReport
make_p18_reference_verification_report(const P18LoadedReferenceFixture &fixture,
                                       const P18ReferenceRenderStats &render_stats,
                                       const P18ReferenceArtifactSet &artifact_set,
                                       std::chrono::nanoseconds render_duration,
                                       std::string_view source_commit) {
    const auto &catalog = p18_reference_catalog_v1();
    std::array<P18ReferenceArtifactRecord, kP18ReferenceAudioArtifactCount> records{};
    bool artifacts_match = true;
    for (std::size_t index = 0; index < catalog.expected_audio.size(); ++index) {
        const auto &description = catalog.expected_audio[index];
        if (&require_expected_audio(catalog, description.audio) != &description) {
            throw std::logic_error{
                "P1.8 audio catalog contains a duplicate enum identity"};
        }
        const auto record = artifact_set.record(description.audio);
        if (!record.has_value()) {
            throw std::logic_error{
                "P1.8 verification requires all eight sealed audio artifacts"};
        }
        records[index] = *record;
        artifacts_match =
            artifacts_match && record->payload_sha256 == description.expected_sha256;
    }

    const std::array mastering_hashes{
        render_stats.raw_float32_payload_sha256,
        render_stats.monitoring_float32_payload_sha256,
        render_stats.faded_float32_payload_sha256,
        render_stats.s32le_payload_sha256,
        render_stats.pcm24le_payload_sha256,
    };
    bool mastering_matches = true;
    for (std::size_t index = 0; index < mastering_hashes.size(); ++index) {
        const auto &expected = catalog.expected_mastering[index];
        if (&require_expected_mastering(catalog, expected.payload) != &expected) {
            throw std::logic_error{
                "P1.8 mastering catalog contains a duplicate enum identity"};
        }
        mastering_matches =
            mastering_matches && mastering_hashes[index] == expected.expected_sha256;
    }

    const auto record_for =
        [&](P18ReferenceAudioArtifact artifact) -> const P18ReferenceArtifactRecord & {
        static_cast<void>(require_expected_audio(catalog, artifact));
        return records[static_cast<std::size_t>(artifact)];
    };
    const bool selected_matches_configured =
        record_for(P18ReferenceAudioArtifact::exhaust_0_configured_ir).payload_sha256 ==
            record_for(P18ReferenceAudioArtifact::exhaust_0_selected).payload_sha256 &&
        record_for(P18ReferenceAudioArtifact::exhaust_1_configured_ir).payload_sha256 ==
            record_for(P18ReferenceAudioArtifact::exhaust_1_selected).payload_sha256;
    const bool counts_match =
        render_stats.input_frame_count ==
            catalog.expected_render.expected_input_frame_count &&
        render_stats.processed_block_count ==
            catalog.expected_render.expected_processed_block_count &&
        render_stats.warmup_block_count ==
            catalog.expected_render.expected_warmup_block_count &&
        render_stats.published_block_count ==
            catalog.expected_render.expected_published_block_count &&
        render_stats.processed_source_frame_count ==
            catalog.expected_render.expected_processed_source_frame_count &&
        render_stats.warmup_source_frame_count ==
            catalog.expected_render.expected_warmup_source_frame_count &&
        render_stats.published_source_frame_count ==
            catalog.expected_render.expected_published_source_frame_count;
    const bool mastering_shape_matches =
        render_stats.saturation_count ==
            catalog.expected_render.expected_saturation_count &&
        std::bit_cast<std::uint32_t>(render_stats.faded_absolute_peak) ==
            catalog.expected_render.expected_faded_absolute_peak_binary32_bits;
    const auto &derived_kernel = fixture.derived_identities.configured_ir_kernel_f64le;
    const auto &derived_spectrum =
        fixture.derived_identities.configured_ir_kernel_spectrum_f64le;
    const bool kernel_matches =
        derived_kernel.byte_count ==
            catalog.expected_kernel.expected_coefficient_count * sizeof(double) &&
        derived_kernel.payload_sha256 ==
            catalog.expected_kernel.expected_coefficient_f64le_sha256;
    const bool spectrum_matches =
        derived_spectrum.payload_sha256 ==
        catalog.expected_kernel.expected_spectrum_f64le_sha256;
    const auto kernel_hash = digest_hex(derived_kernel.payload_sha256);
    const auto spectrum_hash = digest_hex(derived_spectrum.payload_sha256);
    const bool exact_match = kernel_matches && spectrum_matches && artifacts_match &&
                             mastering_matches && selected_matches_configured &&
                             counts_match && mastering_shape_matches;

    std::ostringstream verification;
    verification << std::fixed << std::setprecision(6);
    verification << "P1.8 BMW M52B28 reference presentation verification\n"
                 << "claim=local-evaluation exhaust-only baseline; not a "
                    "higher-fidelity or production-complete engine\n"
                 << "source_commit=" << source_commit << '\n'
                 << "compiler=" << compiler_identity() << '\n'
                 << "exact_reference_match=" << (exact_match ? "yes" : "no") << '\n'
                 << "preflight_seconds=" << seconds(fixture.preflight_duration) << '\n'
                 << "render_and_write_seconds=" << seconds(render_duration) << '\n'
                 << "input_frames=" << render_stats.input_frame_count << '\n'
                 << "processed_blocks=" << render_stats.processed_block_count << '\n'
                 << "warmup_blocks=" << render_stats.warmup_block_count << '\n'
                 << "processed_source_frames="
                 << render_stats.processed_source_frame_count << '\n'
                 << "warmup_source_frames=" << render_stats.warmup_source_frame_count
                 << '\n'
                 << "published_frames=" << render_stats.published_source_frame_count
                 << '\n'
                 << "saturation_count=" << render_stats.saturation_count << '\n'
                 << "faded_peak_bits=0x" << std::hex << std::setfill('0')
                 << std::setw(8)
                 << std::bit_cast<std::uint32_t>(render_stats.faded_absolute_peak)
                 << std::dec << std::setfill(' ') << '\n'
                 << "selected_equals_configured_ir="
                 << (selected_matches_configured ? "yes" : "no") << "\n\n"
                 << "verified_fixture_lineage\n";
    for (const auto &expected : catalog.expected_lineage_files) {
        const auto &observed = fixture.verified_lineage.at(expected.file);
        verification << expected.expected_relative_path << '='
                     << digest_hex(observed.payload_sha256)
                     << " bytes=" << observed.byte_count << '\n';
    }
    verification << "configured_ir_kernel_f64le=" << kernel_hash << " expected="
                 << digest_hex(
                        catalog.expected_kernel.expected_coefficient_f64le_sha256)
                 << " match=" << (kernel_matches ? "yes" : "no") << '\n'
                 << "configured_ir_kernel_spectrum_f64le=" << spectrum_hash
                 << " expected="
                 << digest_hex(catalog.expected_kernel.expected_spectrum_f64le_sha256)
                 << " match=" << (spectrum_matches ? "yes" : "no")
                 << "\n\naudio_artifacts\n";
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto actual = digest_hex(records[index].payload_sha256);
        const auto &expected = catalog.expected_audio[index];
        verification << records[index].relative_path << '=' << actual
                     << " expected=" << digest_hex(expected.expected_sha256)
                     << " match="
                     << (records[index].payload_sha256 == expected.expected_sha256
                             ? "yes"
                             : "no")
                     << '\n';
    }
    verification << "\nmastering_payloads\n";
    for (std::size_t index = 0; index < mastering_hashes.size(); ++index) {
        const auto actual = digest_hex(mastering_hashes[index]);
        const auto &expected = catalog.expected_mastering[index];
        verification << mastering_label(expected.payload) << '=' << actual
                     << " expected=" << digest_hex(expected.expected_sha256)
                     << " match="
                     << (mastering_hashes[index] == expected.expected_sha256 ? "yes"
                                                                             : "no")
                     << '\n';
    }

    std::ostringstream listening;
    listening << "# BMW M52B28 P1.8 listening checkpoint\n\n"
              << "Candidate status: **"
              << (exact_match ? "byte-identical to the liked baseline"
                              : "complete but not byte-identical; listen before "
                                "diagnosing or accepting")
              << "**.\n\n";
    append_listening_link(
        listening, "Main audition",
        require_expected_audio(catalog, P18ReferenceAudioArtifact::master_audition));
    append_listening_link(
        listening, "Unmastered coherent sum",
        require_expected_audio(catalog, P18ReferenceAudioArtifact::master_raw));
    append_listening_link(
        listening, "Route 0 selected",
        require_expected_audio(catalog, P18ReferenceAudioArtifact::exhaust_0_selected));
    append_listening_link(
        listening, "Route 1 selected",
        require_expected_audio(catalog, P18ReferenceAudioArtifact::exhaust_1_selected));
    listening << "\nPreserved oracle: `reference/oracles/bmw-m52b28/"
                 "bmw-m52b28-5th-gear-equivalent-dyno-1500-6500rpm.wav`\n\n"
              << "This checkpoint reproduces the known-good exhaust presentation "
                 "baseline. It is not yet an offline-fidelity improvement, and work "
                 "stops here for listening.\n";

    return {exact_match, verification.str(), listening.str()};
}

} // namespace engine_sim_offline::reference
