#pragma once

#include <iosfwd>

namespace engine_sim_offline::reference {

// Executes the frozen, reference-only BMW cross-RPM fixed-sample diagnostic and
// writes one deterministic human-readable report. The diagnostic publishes no
// artifacts and does not alter the simulation or convergence policy. Any request,
// runtime, or evidence disagreement is reported by throwing std::runtime_error.
void run_bmw_m52b28_cross_rpm_fixed_sample_diagnostic(std::ostream &output);

} // namespace engine_sim_offline::reference
