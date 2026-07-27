#pragma once

#include "determinism/renderer_determinism_envelope.hpp"
#include "engine_sim_offline/contract/render_manifest.hpp"
#include "reference/p18_reference_artifact_set.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_provenance.hpp"

#include <utility>

namespace engine_sim_offline::reference {

// Deterministic manifest content for the frozen trace-driven reference route. This
// value deliberately stops before execution facts, wire encoding, and publication.
// It retains the exact provenance ledger used for successful contract validation so
// the production session can consume the validated pair together.
class P18ReferenceManifestContent final {
  public:
    P18ReferenceManifestContent(const P18ReferenceManifestContent &) = default;
    P18ReferenceManifestContent(P18ReferenceManifestContent &&) noexcept = default;
    P18ReferenceManifestContent &
    operator=(const P18ReferenceManifestContent &) = delete;
    P18ReferenceManifestContent &
    operator=(P18ReferenceManifestContent &&) noexcept = delete;

    [[nodiscard]] const contract::RenderManifestContent &content() const noexcept;
    [[nodiscard]] const P18ReferenceProvenance &provenance() const noexcept;

  private:
    P18ReferenceManifestContent(P18ReferenceProvenance provenance,
                                contract::RenderManifestContent content)
        : provenance_(std::move(provenance)), content_(std::move(content)) {}

    P18ReferenceProvenance provenance_;
    contract::RenderManifestContent content_;

    friend P18ReferenceManifestContent make_p18_reference_manifest_content(
        const P18LoadedReferenceFixture &fixture,
        const P18ReferenceArtifactSet &artifacts,
        const determinism::RendererDeterminismEnvelope &renderer_identity);
};

// The sole production factory accepts only evidence-bearing values:
// - P18LoadedReferenceFixture couples decoded execution inputs to verified lineage;
// - P18ReferenceArtifactSet exposes records only after actual bytes were sealed; and
// - RendererDeterminismEnvelope carries the production mark only from live observers.
//
// The artifact set must still be healthy and open, with all eight audio records
// sealed.
// Expected catalog payload hashes are never accepted or copied as observations; they
// compare the independently sealed records before those records are copied. Throws
// if comparison, construction, or complete contract validation fails.
[[nodiscard]] P18ReferenceManifestContent make_p18_reference_manifest_content(
    const P18LoadedReferenceFixture &fixture, const P18ReferenceArtifactSet &artifacts,
    const determinism::RendererDeterminismEnvelope &renderer_identity);

} // namespace engine_sim_offline::reference
