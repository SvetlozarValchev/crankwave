#pragma once

#include "engine_sim_offline/contract.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace engine_sim_offline {

enum class RenderSinkErrorKind : std::uint8_t {
    publication_failure,
    protocol_violation,
};

struct RenderSinkError {
    RenderSinkErrorKind kind = RenderSinkErrorKind::publication_failure;
    std::string detail_code;
    std::string message;

    friend bool operator==(const RenderSinkError &, const RenderSinkError &) = default;
};

// No value means that the sink operation completed.
using RenderSinkStatus = std::optional<RenderSinkError>;

struct PendingArtifact {
    std::string role;
    contract::ArtifactKind kind = contract::ArtifactKind::unspecified;
    std::string relative_path;
    std::optional<contract::AudioContract> audio;
    bool diagnostic = false;

    friend bool operator==(const PendingArtifact &, const PendingArtifact &) = default;
};

// Both borrowed views are callback-scoped. A sink consumes them before returning.
// Offsets for each role are contiguous and begin at zero.
struct ArtifactChunk {
    std::string_view role;
    std::uint64_t byte_offset = 0;
    std::span<const std::byte> bytes;
};

// One sink instance represents one serial publication transaction. A successful
// begin enters the transaction. A declare/write/seal failure is followed by exactly
// one abort. Commit is a terminal attempt: the sink owns staging cleanup after it
// returns, whether it succeeds or fails.
class RenderSink {
  public:
    virtual ~RenderSink() = default;

    [[nodiscard]] virtual RenderSinkStatus
    begin_transaction(const contract::OutputContract &output_contract) = 0;
    [[nodiscard]] virtual RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) = 0;
    [[nodiscard]] virtual RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) = 0;
    [[nodiscard]] virtual RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) = 0;
    [[nodiscard]] virtual RenderSinkStatus
    commit(const contract::RenderManifest &manifest) = 0;
    virtual void abort() noexcept = 0;
};

struct RenderControl {
    std::stop_token stop_token;
};

} // namespace engine_sim_offline
