#pragma once

#include <iosfwd>

namespace engine_sim_offline::reference {

// Executes the frozen, reference-only BMW 2500-rpm three-cutoff settling
// diagnostic and writes one deterministic human-readable report. The diagnostic
// publishes no artifacts and does not alter the simulation or convergence policy.
// Any request, runtime, evidence, or reproduction disagreement is reported by
// throwing std::runtime_error.
void run_bmw_m52b28_held_settling_diagnostic(std::ostream &output);

} // namespace engine_sim_offline::reference
