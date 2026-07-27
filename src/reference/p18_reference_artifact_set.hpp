#pragma once

#include "engine_sim_offline/artifacts/wav_encoder.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/render.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::reference {

enum class P18ReferenceAudioArtifact : std::uint8_t {
    exhaust_0_dry,
    exhaust_0_configured_ir,
    exhaust_0_selected,
    exhaust_1_dry,
    exhaust_1_configured_ir,
    exhaust_1_selected,
    master_raw,
    master_audition,
};

inline constexpr std::size_t kP18ReferenceAudioArtifactCount = 8;
inline constexpr std::uint64_t kP18ReferenceFloatWaveByteCount = 11'520'058;
inline constexpr std::uint64_t kP18ReferenceAuditionWaveByteCount = 8'640'302;
inline constexpr std::size_t kP18ReferenceMaximumReportBytes = 64U * 1024U;
inline constexpr std::size_t kP18ReferenceMaximumReportCount = 8;

struct P18ReferenceAudioArtifactDescription {
    P18ReferenceAudioArtifact artifact = P18ReferenceAudioArtifact::exhaust_0_dry;
    std::string_view role;
    std::string_view relative_path;
    std::uint64_t exact_byte_count = 0;
};

// The returned descriptions are immutable and ordered by P18ReferenceAudioArtifact.
[[nodiscard]] std::span<const P18ReferenceAudioArtifactDescription,
                        kP18ReferenceAudioArtifactCount>
p18_reference_audio_artifacts() noexcept;

struct P18ReferenceArtifactRecord {
    P18ReferenceAudioArtifact artifact = P18ReferenceAudioArtifact::exhaust_0_dry;
    std::string_view role;
    std::string_view relative_path;
    std::uint64_t byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const P18ReferenceArtifactRecord &,
                           const P18ReferenceArtifactRecord &) = default;
};

enum class P18ReferenceArtifactSetState : std::uint8_t {
    open,
    published,
    aborted,
};

// Reference-only publication transaction for the fixed P1.8 listening set. It is
// intentionally narrower than DirectoryRenderSink: the eight audio paths and byte
// counts are frozen here, while manifest publication remains a later checkpoint.
//
// create() requires an existing real directory and a single conservative portable
// publication-name component. It creates all eight files exclusively under one
// private sibling staging directory. publish() makes that directory visible with an
// atomic no-replace rename. A failed or abandoned transaction removes only the
// staging inode recorded by create(). Linux is the supported publication platform.
class P18ReferenceArtifactSet final {
  public:
    using CreateResult =
        std::variant<std::unique_ptr<P18ReferenceArtifactSet>, RenderSinkError>;

    [[nodiscard]] static CreateResult create(std::filesystem::path publication_root,
                                             std::string publication_name);
    ~P18ReferenceArtifactSet();

    P18ReferenceArtifactSet(const P18ReferenceArtifactSet &) = delete;
    P18ReferenceArtifactSet &operator=(const P18ReferenceArtifactSet &) = delete;
    P18ReferenceArtifactSet(P18ReferenceArtifactSet &&) = delete;
    P18ReferenceArtifactSet &operator=(P18ReferenceArtifactSet &&) = delete;

    // The callback is valid only while this set remains alive. It accepts bounded,
    // contiguous chunks beginning at byte offset zero and is directly compatible
    // with WavEncoder and P18AuditionWaveEncoder. A rejected chunk poisons the
    // transaction; last_error() retains the reason.
    [[nodiscard]] artifacts::WavChunkConsumer
    consumer(P18ReferenceAudioArtifact artifact);

    // Sealing checks the fixed exact byte count, synchronizes the file, captures its
    // actual SHA-256, and closes its writable descriptor. No oracle hash is consulted:
    // a complete non-matching render remains publishable for listening and diagnosis.
    [[nodiscard]] RenderSinkStatus seal(P18ReferenceAudioArtifact artifact);

    // Adds a small report after all audio files have been sealed. Reports must use a
    // new portable relative path outside audio/, contain no NUL bytes, and are also
    // created exclusively, synchronized, hashed, and included in exact inventory
    // verification.
    [[nodiscard]] RenderSinkStatus write_text_report(std::string relative_path,
                                                     std::string_view contents);

    [[nodiscard]] RenderSinkStatus publish();
    void abort() noexcept;

    [[nodiscard]] P18ReferenceArtifactSetState state() const noexcept;
    [[nodiscard]] std::filesystem::path publication_path() const;
    [[nodiscard]] std::optional<std::filesystem::path> staging_path() const;
    [[nodiscard]] std::optional<P18ReferenceArtifactRecord>
    record(P18ReferenceAudioArtifact artifact) const noexcept;
    [[nodiscard]] std::optional<RenderSinkError> last_error() const;

  private:
    class Implementation;

    P18ReferenceArtifactSet(std::filesystem::path publication_root,
                            std::string publication_name);
    std::unique_ptr<Implementation> implementation_;
};

} // namespace engine_sim_offline::reference
