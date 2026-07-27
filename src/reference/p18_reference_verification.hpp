#pragma once

#include "determinism/renderer_determinism_envelope.hpp"
#include "reference/p18_reference_artifact_set.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_render_session.hpp"

#include <chrono>
#include <string>

namespace engine_sim_offline::reference {

struct P18ReferenceVerificationReport {
    bool exact_reference_match = false;
    std::string verification_text;
    std::string listening_markdown;
};

// Compares a complete candidate against the frozen local-evaluation identities and
// builds the two small reports published beside it. A mismatch is reported but is not
// a render/publication failure: the listening gate, not a digest, decides acceptance.
[[nodiscard]] P18ReferenceVerificationReport make_p18_reference_verification_report(
    const P18LoadedReferenceFixture &fixture,
    const P18ReferenceRenderStats &render_stats,
    const P18ReferenceArtifactSet &artifacts, std::chrono::nanoseconds render_duration,
    const determinism::RendererDeterminismEnvelope &renderer_identity);

} // namespace engine_sim_offline::reference
