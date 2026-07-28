#pragma once

#include "engine_sim_offline/contract.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline {

struct RenderAssetPayload {
    contract::AudioAssetId id;
    std::vector<std::byte> bytes;

    friend bool operator==(const RenderAssetPayload &,
                           const RenderAssetPayload &) = default;
};

// The resolved, immutable input selected for one render. The scenario remains a
// separate argument so request/result matching is explicit at the API boundary.
struct RenderSpecification {
    contract::EngineSpec engine;
    contract::PresentationCalibration presentation;
    contract::ResolvedRandomnessPolicy randomness;
    contract::ProvenanceLedger provenance;
    contract::SourceMatrixContract source_matrix;
    std::vector<RenderAssetPayload> asset_payloads;

    friend bool operator==(const RenderSpecification &,
                           const RenderSpecification &) = default;
};

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

// No value means that the sink operation completed. An admitted execution route maps
// publication failures to artifact_publication_failure and protocol failures to
// contract_violation.
using RenderSinkStatus = std::optional<RenderSinkError>;

struct PendingArtifact {
    std::string role;
    contract::ArtifactKind kind = contract::ArtifactKind::unspecified;
    std::string relative_path;
    std::optional<contract::AudioContract> audio;
    bool diagnostic = false;

    friend bool operator==(const PendingArtifact &, const PendingArtifact &) = default;
};

// Both borrowed views are callback-scoped. A sink must consume them before returning
// and may not retain either view. Offsets for each role are contiguous and begin at
// zero.
struct ArtifactChunk {
    std::string_view role;
    std::uint64_t byte_offset = 0;
    std::span<const std::byte> bytes;
};

// One RenderSink instance represents one transaction and is called serially by one
// render session. Successful begin_transaction enters begun; a failed begin is atomic
// and leaves the sink idle. A declare/write/seal failure is followed by exactly one
// abort. commit is a terminal attempt: success publishes atomically, while failure
// atomically removes staging and leaves the transaction aborted, so render does not
// call abort after commit returns. abort is noexcept and idempotent.
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

// Synchronous and session-local. Complete preflight occurs before an opaque admitted
// job can begin the sink transaction. Unsupported routes fail closed; the renderer
// never substitutes silence, a tone, a legacy renderer, or fixture data.
[[nodiscard]] contract::RenderResult render(const RenderSpecification &specification,
                                            const contract::RenderScenario &scenario,
                                            RenderSink &sink,
                                            RenderControl control = {});

// Rebinds a result to the complete render-layer request, including engine,
// presentation, randomness, assets, provenance, and source policy. This is stricter
// than the lower-level contract validator, which cannot see RenderSpecification.
[[nodiscard]] contract::ValidationReport
validate(const contract::RenderResult &result, const RenderSpecification &specification,
         const contract::RenderScenario &scenario);

} // namespace engine_sim_offline
