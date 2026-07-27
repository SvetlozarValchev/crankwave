#pragma once

#include "dsp/p18_fixed_fft.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "reference/p18_reference_audit_reader.hpp"
#include "reference/p18_reference_seed_reader.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace engine_sim_offline::reference {

inline constexpr std::size_t kP18ReferenceConfiguredIrWaveByteCount = 78602;
inline constexpr std::size_t kP18ReferenceConfiguredIrSampleCount = 33705;
inline constexpr std::size_t kP18ReferenceConfiguredIrSupportFrames = 6907;
inline constexpr std::uint64_t kP18ReferenceConfiguredIrGainBits =
    UINT64_C(0x3f50624dd2f1a9fc);

struct P18ReferenceFixtureDigests {
    contract::Sha256Digest reference_audit;
    contract::Sha256Digest component_seeds;
    contract::Sha256Digest configured_ir_wave;
    // SHA-256 over coefficient IEEE-754 bits serialized explicitly as f64le.
    contract::Sha256Digest configured_ir_kernel_f64le;
    // SHA-256 over index-ordered complex spectrum pairs as real-f64le then
    // imaginary-f64le. Both derived identities diagnose implementation drift
    // without preventing a complete candidate from being rendered and heard.
    contract::Sha256Digest configured_ir_kernel_spectrum_f64le;

    friend bool operator==(const P18ReferenceFixtureDigests &,
                           const P18ReferenceFixtureDigests &) = default;
};

struct P18LoadedReferenceFixture {
    P18DecodedReferenceAudit audit;
    P18DecodedReferenceSeeds component_seeds;
    std::vector<double> configured_ir_coefficients;
    std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir_kernel;
    P18ReferenceFixtureDigests digests;
    std::chrono::nanoseconds preflight_duration{};
};

// Reference-only, fail-closed preflight for the frozen BMW P1.8 fixture. The
// caller selects only the root; fixed descendants are opened once and bounded by
// their exact frozen sizes. Linux opens reject symbolic links at every descendant.
// Expected outputs and oracle media are deliberately outside this input boundary.
// Throws std::runtime_error (or a derived standard exception) on every failure.
[[nodiscard]] P18LoadedReferenceFixture
load_p18_reference_fixture(const std::filesystem::path &fixture_root);

} // namespace engine_sim_offline::reference
