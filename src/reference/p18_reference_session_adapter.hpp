#pragma once

#include "reference/p18_reference_audit_reader.hpp"
#include "reference/p18_reference_render_session.hpp"
#include "reference/p18_reference_seed_reader.hpp"

#include <array>

namespace engine_sim_offline::reference {

// Builds the frozen reference publication policy without importing expected
// payload identities into the presentation session.
[[nodiscard]] P18PresentationSessionPlan make_p18_reference_presentation_session_plan();

// Converts the fixture reader's deliberately presentation-free seed inventory at
// the narrow reference/session boundary.
[[nodiscard]] std::array<presentation::P18RouteConditioningSeeds, 2>
p18_reference_presentation_seeds(const P18DecodedReferenceSeeds &seeds);

// Replays the complete, already-decoded audit through the fixture-free session in
// the exact method-block shape accepted by the P1.8 source stage.
void replay_p18_reference_audit(const P18DecodedReferenceAudit &audit,
                                P18PresentationSession &session);

} // namespace engine_sim_offline::reference
