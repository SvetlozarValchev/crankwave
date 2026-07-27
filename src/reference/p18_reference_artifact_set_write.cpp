#include "p18_reference_artifact_set_impl.hpp"

#include <cerrno>
#include <span>
#include <string>
#include <utility>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::reference {

using p18_artifact_set_detail::protocol_error;
using p18_artifact_set_detail::publication_error;

bool P18ReferenceArtifactSet::Implementation::write(P18ReferenceAudioArtifact artifact,
                                                    std::uint64_t byte_offset,
                                                    std::span<const std::byte> bytes) {
    if (require_writable("audio write").has_value()) {
        return false;
    }
    const auto index = p18_artifact_set_detail::artifact_index(artifact);
    if (!index.has_value()) {
        static_cast<void>(poison(protocol_error(
            "p18-audio-artifact-invalid", "unknown P1.8 audio artifact identity")));
        return false;
    }
    auto &audio = audio_[*index];
    const auto &description = p18_artifact_set_detail::audio_artifacts()[*index];
    if (audio.sealed) {
        static_cast<void>(poison(protocol_error(
            "p18-audio-artifact-sealed",
            "a sealed P1.8 audio artifact cannot accept another chunk")));
        return false;
    }
    if (bytes.empty() || bytes.size() > artifacts::kMaximumArtifactEncoderChunkBytes) {
        static_cast<void>(poison(protocol_error(
            "p18-audio-chunk-size-invalid",
            "P1.8 audio chunks must be nonempty and no larger than 64 KiB")));
        return false;
    }
    if (byte_offset != audio.byte_count) {
        static_cast<void>(poison(protocol_error(
            "p18-audio-offset-noncontiguous",
            "P1.8 audio chunk offsets must begin at zero and remain contiguous")));
        return false;
    }
    if (byte_offset > description.expected_byte_count ||
        bytes.size() > description.expected_byte_count - byte_offset) {
        static_cast<void>(poison(
            protocol_error("p18-audio-byte-count-exceeded",
                           "P1.8 audio bytes exceed the frozen artifact byte count")));
        return false;
    }

#if defined(__linux__)
    if (!artifacts::detail::write_all_at(audio.file.get(), audio.byte_count, bytes)) {
        static_cast<void>(poison(publication_error(
            "p18-audio-write-failed",
            artifacts::detail::errno_message(
                "could not write a complete P1.8 audio chunk", errno))));
        return false;
    }
    audio.hash.update(bytes);
    audio.byte_count += static_cast<std::uint64_t>(bytes.size());
    return true;
#else
    static_cast<void>(bytes);
    static_cast<void>(poison(publication_error(
        "p18-atomic-noreplace-unavailable",
        "P1.8 reference publication is currently supported only on Linux")));
    return false;
#endif
}

RenderSinkStatus
P18ReferenceArtifactSet::Implementation::seal(P18ReferenceAudioArtifact artifact) {
    if (auto error = require_writable("audio seal")) {
        return error;
    }
    const auto index = p18_artifact_set_detail::artifact_index(artifact);
    if (!index.has_value()) {
        return poison(protocol_error("p18-audio-artifact-invalid",
                                     "unknown P1.8 audio artifact identity"));
    }
    auto &audio = audio_[*index];
    const auto &description = p18_artifact_set_detail::audio_artifacts()[*index];
    if (audio.sealed) {
        return poison(
            protocol_error("p18-audio-artifact-sealed",
                           "a P1.8 audio artifact may be sealed exactly once"));
    }
    if (audio.byte_count != description.expected_byte_count) {
        return poison(protocol_error(
            "p18-audio-byte-count-mismatch",
            "P1.8 audio artifact is incomplete or exceeds its frozen byte count"));
    }

#if defined(__linux__)
    if (::fsync(audio.file.get()) == -1) {
        return poison(publication_error(
            "p18-audio-sync-failed",
            artifacts::detail::errno_message(
                "could not synchronize a staged P1.8 audio artifact", errno)));
    }
    struct stat status{};
    if (::fstat(audio.file.get(), &status) == -1 || !S_ISREG(status.st_mode) ||
        status.st_nlink != 1 || status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) != description.expected_byte_count) {
        return poison(publication_error(
            "p18-audio-file-verification-failed",
            "staged P1.8 audio is not one private regular file of the exact size"));
    }
    const auto observed_byte_count = static_cast<std::uint64_t>(status.st_size);
    const auto digest = audio.hash.finish();
    audio.identity = FileIdentity{std::string(description.expected_relative_path),
                                  observed_byte_count, digest,
                                  static_cast<std::uintmax_t>(status.st_dev),
                                  static_cast<std::uintmax_t>(status.st_ino)};
    audio.record = P18ReferenceArtifactRecord{artifact, description.expected_role,
                                              description.expected_relative_path,
                                              observed_byte_count, digest};
    audio.file.reset();
    audio.sealed = true;
    return std::nullopt;
#else
    return poison(publication_error(
        "p18-atomic-noreplace-unavailable",
        "P1.8 reference publication is currently supported only on Linux"));
#endif
}

RenderSinkStatus
P18ReferenceArtifactSet::Implementation::write_text_report(std::string relative_path,
                                                           std::string_view contents) {
    if (auto error = require_writable("report write")) {
        return error;
    }
    if (!all_audio_sealed()) {
        return poison(protocol_error(
            "p18-report-before-audio-seal",
            "P1.8 reports may be added only after all eight audio files are sealed"));
    }
    if (reports_.size() == kP18ReferenceMaximumReportCount) {
        return poison(protocol_error(
            "p18-report-count-exceeded",
            "P1.8 publication accepts at most eight small report files"));
    }
    if (!artifacts::detail::valid_relative_path(relative_path)) {
        return poison(protocol_error(
            "p18-report-path-invalid",
            "P1.8 report path must be a conservative portable relative path"));
    }
    const auto components = artifacts::detail::path_components(relative_path);
    if (components.empty() ||
        artifacts::detail::portable_path_key(components.front()) == "audio") {
        return poison(protocol_error(
            "p18-report-inside-audio",
            "P1.8 text reports must be stored outside the reserved audio directory"));
    }
    if (std::ranges::any_of(reserved_paths_, [&](const std::string &existing) {
            return artifacts::detail::portable_paths_conflict(existing, relative_path);
        })) {
        return poison(protocol_error(
            "p18-report-path-conflict",
            "P1.8 report path conflicts with an audio or prior report path"));
    }
    if (contents.empty() || contents.size() > kP18ReferenceMaximumReportBytes ||
        contents.find('\0') != std::string_view::npos) {
        return poison(protocol_error(
            "p18-report-content-invalid",
            "P1.8 report must contain 1 through 65536 text bytes and no NUL"));
    }

#if defined(__linux__)
    reserved_paths_.insert(relative_path);
    auto created =
        artifacts::detail::create_file_beneath(stage_fd_.get(), relative_path);
    if (auto *error = std::get_if<RenderSinkError>(&created)) {
        return poison(std::move(*error));
    }
    auto file = std::move(std::get<artifacts::detail::FileDescriptor>(created));
    const auto bytes = std::as_bytes(std::span{contents.data(), contents.size()});
    if (!artifacts::detail::write_all_at(file.get(), 0, bytes)) {
        return poison(
            publication_error("p18-report-write-failed",
                              artifacts::detail::errno_message(
                                  "could not write complete P1.8 text report", errno)));
    }
    if (::fsync(file.get()) == -1) {
        return poison(publication_error(
            "p18-report-sync-failed",
            artifacts::detail::errno_message(
                "could not synchronize a staged P1.8 text report", errno)));
    }
    struct stat status{};
    if (::fstat(file.get(), &status) == -1 || !S_ISREG(status.st_mode) ||
        status.st_nlink != 1 || status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) != bytes.size()) {
        return poison(publication_error(
            "p18-report-file-verification-failed",
            "staged P1.8 report is not one private regular file of the exact size"));
    }
    contract::detail::Sha256Stream hash;
    hash.update(bytes);
    reports_.push_back({std::move(relative_path),
                        static_cast<std::uint64_t>(bytes.size()), hash.finish(),
                        static_cast<std::uintmax_t>(status.st_dev),
                        static_cast<std::uintmax_t>(status.st_ino)});
    return std::nullopt;
#else
    static_cast<void>(relative_path);
    static_cast<void>(contents);
    return poison(publication_error(
        "p18-atomic-noreplace-unavailable",
        "P1.8 reference publication is currently supported only on Linux"));
#endif
}

} // namespace engine_sim_offline::reference
