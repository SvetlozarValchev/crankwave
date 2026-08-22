#pragma once

#include "crankwave/bake.hpp"
#include "crankwave/compile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace crankwave::test::bmw_m52b28_render_gate {

void expect(bool condition, std::string_view message);

[[nodiscard]] std::string
digest_hex(const contract::Sha256Digest &digest);
[[nodiscard]] contract::Sha256Digest
digest_from_hex(std::string_view text);
[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path);

// This fixture deliberately exercises only the public parse/compile boundary.
[[nodiscard]] compile::CompiledScenario
compile_authored_scenario(const std::filesystem::path &repository_root);
[[nodiscard]] compile::CompiledScenario
compile_authored_free_engine_scenario(
    const std::filesystem::path &repository_root);
[[nodiscard]] compile::CompiledScenario
compile_authored_bmw_m52tub28_cold_start_scenario(
    const std::filesystem::path &repository_root);
[[nodiscard]] compile::CompiledScenario
compile_authored_bmw_m52tub28_held_dyno_scenario(
    const std::filesystem::path &repository_root);
[[nodiscard]] compile::CompiledScenario
compile_authored_bmw_m52tub28_free_vehicle_scenario(
    const std::filesystem::path &repository_root);

struct StoredArtifact {
    PendingArtifact declaration;
    std::vector<std::byte> bytes;
    bool sealed = false;
    std::optional<contract::ArtifactRecord> record;
};

class VerifyingMemorySink final : public RenderSink {
  public:
    std::size_t begin_calls = 0;
    std::size_t declaration_calls = 0;
    std::size_t write_calls = 0;
    std::size_t seal_calls = 0;
    std::size_t commit_calls = 0;
    std::size_t abort_calls = 0;
    std::optional<contract::OutputContract> output_contract;
    std::vector<StoredArtifact> artifacts;
    std::vector<contract::ArtifactRecord> seals;
    std::optional<contract::RenderManifest> committed_manifest;

    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &contract) override;
    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override;
    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override;
    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override;
    [[nodiscard]] RenderSinkStatus
    commit(const contract::RenderManifest &manifest) override;
    void abort() noexcept override;

    [[nodiscard]] const StoredArtifact &at(std::string_view role) const;

  private:
    enum class State : std::uint8_t {
        idle,
        begun,
        committed,
        aborted,
    };

    [[nodiscard]] RenderSinkStatus protocol_error(std::string message) const;

    State state_ = State::idle;
    std::unordered_map<std::string, std::size_t> role_index_;
};

struct RenderIdentityObservation {
    std::string simulation_request_sha256;
    std::uint64_t audition_wave_byte_count = 0;
    std::string audition_wave_sha256;
};

// Verifies the complete generic manifest/container surface and returns its
// pinned Crankwave render identities.
[[nodiscard]] RenderIdentityObservation verify_render_success(
    const contract::RenderSuccess &success, const VerifyingMemorySink &sink);

} // namespace crankwave::test::bmw_m52b28_render_gate
