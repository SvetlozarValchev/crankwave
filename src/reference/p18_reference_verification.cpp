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

constexpr std::array<std::string_view, kP18ReferenceAudioArtifactCount>
    kExpectedArtifactHashes{
        "e5a96cb5d3b9f1732e741916706a99c6a7c5e1a912e3d751cb92846d33ce6eeb",
        "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32",
        "a637639a4ec85d1c6a1432a0b0df2395e3669648f5708f846ce65e83b70e6f32",
        "2ad2ed41097af30421047f3e4a6033086ec70b9731082833699caad1da9d81ea",
        "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d",
        "f47b94024648f6763804fa36bd11bf230d3b5741f2289bb062a6afe7c4a8ba3d",
        "2c5473cfc3836f18164bb2fc52bec11d2a2349ca9fbd550130c520baa3750146",
        "f62c164f9a3debca23b1459fae8d6b47a19a98a418490e2e99bcbdf8a7d972eb",
    };

constexpr std::array<std::string_view, 5> kExpectedMasteringHashes{
    "fe2475249df2f6a51b2c82c8251493216db1a1ec094a7a0c5577a11c430f410f",
    "0a2abe8ea8f166c1022efda26c57e5ad4eda5e7cb515100a5e6d16eb465318db",
    "af194389df2ba20ab9d1bc5e3f97735afbb6c76d4c215a7ac2e1d45ecd3a3633",
    "b0505bc9a81cfdcea0256ff6e5731ac2a1f58f90f43911bc84d799926151d924",
    "2153869958bb924e4eda277a37e95eab1abb7c29aa9fa389c1fa8f879e7bfdcf",
};

constexpr std::string_view kExpectedConfiguredIrKernelSha256 =
    "940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598";
constexpr std::string_view kExpectedConfiguredIrKernelSpectrumSha256 =
    "a1a12fc0224ecdf824e402562ed6b5fd31d915278693a8cfaaeea41a5cf957d2";

constexpr std::array<std::string_view, 5> kMasteringLabels{
    "raw_float32_payload",   "monitoring_float32_payload",
    "faded_float32_payload", "s32le_payload",
    "pcm24le_payload",
};

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    return artifacts::detail::digest_hex(digest);
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

} // namespace

P18ReferenceVerificationReport
make_p18_reference_verification_report(const P18LoadedReferenceFixture &fixture,
                                       const P18ReferenceRenderStats &render_stats,
                                       const P18ReferenceArtifactSet &artifact_set,
                                       std::chrono::nanoseconds render_duration,
                                       std::string_view source_commit) {
    std::array<P18ReferenceArtifactRecord, kP18ReferenceAudioArtifactCount> records{};
    bool artifacts_match = true;
    for (const auto &description : p18_reference_audio_artifacts()) {
        const auto index = static_cast<std::size_t>(description.artifact);
        const auto record = artifact_set.record(description.artifact);
        if (!record.has_value()) {
            throw std::logic_error{
                "P1.8 verification requires all eight sealed audio artifacts"};
        }
        records[index] = *record;
        artifacts_match = artifacts_match && digest_hex(record->payload_sha256) ==
                                                 kExpectedArtifactHashes[index];
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
        mastering_matches = mastering_matches && digest_hex(mastering_hashes[index]) ==
                                                     kExpectedMasteringHashes[index];
    }

    const bool selected_matches_configured =
        records[static_cast<std::size_t>(
                    P18ReferenceAudioArtifact::exhaust_0_configured_ir)]
                .payload_sha256 ==
            records[static_cast<std::size_t>(
                        P18ReferenceAudioArtifact::exhaust_0_selected)]
                .payload_sha256 &&
        records[static_cast<std::size_t>(
                    P18ReferenceAudioArtifact::exhaust_1_configured_ir)]
                .payload_sha256 ==
            records[static_cast<std::size_t>(
                        P18ReferenceAudioArtifact::exhaust_1_selected)]
                .payload_sha256;
    const bool counts_match =
        render_stats.input_frame_count == kP18ReferenceAuditRecordCount &&
        render_stats.processed_block_count == kP18ReferenceProcessedBlockCount &&
        render_stats.warmup_block_count == kP18ReferenceWarmupBlockCount &&
        render_stats.published_block_count == kP18ReferencePublishedBlockCount &&
        render_stats.processed_source_frame_count ==
            kP18ReferenceProcessedSourceFrameCount &&
        render_stats.warmup_source_frame_count == kP18ReferenceWarmupSourceFrameCount &&
        render_stats.published_source_frame_count ==
            kP18ReferencePublishedSourceFrameCount;
    const bool mastering_shape_matches =
        render_stats.saturation_count == 0 &&
        std::bit_cast<std::uint32_t>(render_stats.faded_absolute_peak) ==
            UINT32_C(0x3f2ad253);
    const auto kernel_hash = digest_hex(fixture.digests.configured_ir_kernel_f64le);
    const auto spectrum_hash =
        digest_hex(fixture.digests.configured_ir_kernel_spectrum_f64le);
    const bool kernel_matches = kernel_hash == kExpectedConfiguredIrKernelSha256;
    const bool spectrum_matches =
        spectrum_hash == kExpectedConfiguredIrKernelSpectrumSha256;
    const bool exact_match = kernel_matches && spectrum_matches && artifacts_match &&
                             mastering_matches && selected_matches_configured &&
                             counts_match && mastering_shape_matches;

    std::ostringstream verification;
    verification << std::fixed << std::setprecision(6);
    verification
        << "P1.8 BMW M52B28 reference presentation verification\n"
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
        << "processed_source_frames=" << render_stats.processed_source_frame_count
        << '\n'
        << "warmup_source_frames=" << render_stats.warmup_source_frame_count << '\n'
        << "published_frames=" << render_stats.published_source_frame_count << '\n'
        << "saturation_count=" << render_stats.saturation_count << '\n'
        << "faded_peak_bits=0x" << std::hex << std::setfill('0') << std::setw(8)
        << std::bit_cast<std::uint32_t>(render_stats.faded_absolute_peak) << std::dec
        << std::setfill(' ') << '\n'
        << "selected_equals_configured_ir="
        << (selected_matches_configured ? "yes" : "no") << "\n\n"
        << "fixture_inputs\n"
        << "reference-audit.bin=" << digest_hex(fixture.digests.reference_audit) << '\n'
        << "component-seeds.bin=" << digest_hex(fixture.digests.component_seeds) << '\n'
        << "presentation/smooth_39.wav="
        << digest_hex(fixture.digests.configured_ir_wave) << '\n'
        << "configured_ir_kernel_f64le=" << kernel_hash
        << " expected=" << kExpectedConfiguredIrKernelSha256
        << " match=" << (kernel_matches ? "yes" : "no") << '\n'
        << "configured_ir_kernel_spectrum_f64le=" << spectrum_hash
        << " expected=" << kExpectedConfiguredIrKernelSpectrumSha256
        << " match=" << (spectrum_matches ? "yes" : "no") << "\n\naudio_artifacts\n";
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto actual = digest_hex(records[index].payload_sha256);
        verification << records[index].relative_path << '=' << actual
                     << " expected=" << kExpectedArtifactHashes[index] << " match="
                     << (actual == kExpectedArtifactHashes[index] ? "yes" : "no")
                     << '\n';
    }
    verification << "\nmastering_payloads\n";
    for (std::size_t index = 0; index < mastering_hashes.size(); ++index) {
        const auto actual = digest_hex(mastering_hashes[index]);
        verification << kMasteringLabels[index] << '=' << actual
                     << " expected=" << kExpectedMasteringHashes[index] << " match="
                     << (actual == kExpectedMasteringHashes[index] ? "yes" : "no")
                     << '\n';
    }

    std::ostringstream listening;
    listening << "# BMW M52B28 P1.8 listening checkpoint\n\n"
              << "Candidate status: **"
              << (exact_match ? "byte-identical to the liked baseline"
                              : "complete but not byte-identical; listen before "
                                "diagnosing or accepting")
              << "**.\n\n"
              << "- Main audition: [master.reference.audition.wav](audio/"
                 "master.reference.audition.wav)\n"
              << "- Unmastered coherent sum: [master.reference.raw.wav](audio/"
                 "master.reference.raw.wav)\n"
              << "- Route 0 selected: [exhaust.reference.0.selected.wav](audio/"
                 "exhaust.reference.0.selected.wav)\n"
              << "- Route 1 selected: [exhaust.reference.1.selected.wav](audio/"
                 "exhaust.reference.1.selected.wav)\n\n"
              << "Preserved oracle: `reference/oracles/bmw-m52b28/"
                 "bmw-m52b28-5th-gear-equivalent-dyno-1500-6500rpm.wav`\n\n"
              << "This checkpoint reproduces the known-good exhaust presentation "
                 "baseline. It is not yet an offline-fidelity improvement, and work "
                 "stops here for listening.\n";

    return {exact_match, verification.str(), listening.str()};
}

} // namespace engine_sim_offline::reference
