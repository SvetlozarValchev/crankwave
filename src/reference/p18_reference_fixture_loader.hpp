#pragma once

#include "dsp/p18_fixed_fft.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "reference/p18_reference_audit_reader.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_seed_reader.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace engine_sim_offline::reference {

struct P18ObservedLineageFileIdentity {
    P18ReferenceLineageFile file = P18ReferenceLineageFile::manifest;
    std::uint64_t byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const P18ObservedLineageFileIdentity &,
                           const P18ObservedLineageFileIdentity &) = default;
};

struct P18LoadedReferenceFixture;

// Constructed only after all seven observed files have independently matched the
// immutable catalog. Expected catalog records and observed file identities remain
// different types so later manifest construction cannot silently substitute one for
// the other.
class P18VerifiedReferenceLineage {
  public:
    P18VerifiedReferenceLineage(const P18VerifiedReferenceLineage &) = default;
    P18VerifiedReferenceLineage(P18VerifiedReferenceLineage &&) noexcept = default;
    P18VerifiedReferenceLineage &
    operator=(const P18VerifiedReferenceLineage &) = delete;
    P18VerifiedReferenceLineage &
    operator=(P18VerifiedReferenceLineage &&) noexcept = delete;

    [[nodiscard]] const P18ObservedLineageFileIdentity &
    at(P18ReferenceLineageFile file) const;

    friend bool operator==(const P18VerifiedReferenceLineage &,
                           const P18VerifiedReferenceLineage &) = default;

  private:
    explicit P18VerifiedReferenceLineage(
        std::array<P18ObservedLineageFileIdentity, kP18ReferenceLineageFileCount> files)
        : files_(std::move(files)) {}

    std::array<P18ObservedLineageFileIdentity, kP18ReferenceLineageFileCount> files_;

    friend P18LoadedReferenceFixture
    load_p18_reference_fixture(const std::filesystem::path &fixture_root);
};

struct P18DerivedPayloadIdentity {
    std::uint64_t byte_count = 0;
    contract::Sha256Digest payload_sha256;

    friend bool operator==(const P18DerivedPayloadIdentity &,
                           const P18DerivedPayloadIdentity &) = default;
};

struct P18ReferenceDerivedIdentities {
    // Coefficient bits are regenerated from the configured IR, then compared with
    // the separately streamed kernel-comparator file identity. Comparator bytes are
    // never retained or supplied to convolution.
    P18DerivedPayloadIdentity configured_ir_kernel_f64le;
    // Index-ordered complex spectrum pairs serialized real-f64le then imaginary-f64le.
    P18DerivedPayloadIdentity configured_ir_kernel_spectrum_f64le;

    friend bool operator==(const P18ReferenceDerivedIdentities &,
                           const P18ReferenceDerivedIdentities &) = default;
};

struct P18LoadedReferenceFixture {
    P18DecodedReferenceAudit audit;
    P18DecodedReferenceSeeds component_seeds;
    std::vector<double> configured_ir_coefficients;
    std::shared_ptr<const dsp::P18FixedConvolutionKernel> configured_ir_kernel;
    P18VerifiedReferenceLineage verified_lineage;
    P18ReferenceDerivedIdentities derived_identities;
    std::chrono::nanoseconds preflight_duration{};
};

// Linux-only, reference-only, fail-closed preflight for the frozen BMW P1.8 fixture.
// The caller selects only the root. All seven fixed lineage descendants are opened
// once, bounded, streamed, and content-verified. Only audit, seeds, and configured IR
// bytes are retained and decoded. Parity evidence and the kernel comparator never
// enter DSP; expected stems and oracle media remain outside this input boundary.
// Descriptor-relative opens reject symbolic links at every descendant. Other
// platforms fail explicitly instead of using a pathname check/open sequence.
// Throws std::runtime_error (or a derived standard exception) on every failure.
[[nodiscard]] P18LoadedReferenceFixture
load_p18_reference_fixture(const std::filesystem::path &fixture_root);

} // namespace engine_sim_offline::reference
