#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace engine_sim_offline::reference {

// Every method identity emitted by the isolated P1.8 reference renderer. The enum
// order is canonical and is also the manifest order: input boundary, randomness,
// then presentation.
enum class P18ReferenceMethod : std::uint8_t {
    audit_reader,
    excitation_adapter,
    excitation_seam,
    random_generator,
    seed_derivation,
    reconstruction,
    conditioning,
    impulse_response_conversion,
    convolution,
    publication,
    audition_mix,
};

inline constexpr std::size_t kP18ReferenceMethodCount = 11;

struct P18ReferenceMethodIdentity {
    P18ReferenceMethod method = P18ReferenceMethod::audit_reader;
    std::string_view id;
    std::uint32_t version = 0;
    contract::Sha256Digest configuration_sha256;
    // Canonical UTF-8 bytes, including the final LF. Retained privately so the
    // content-derived identity can be independently recomputed and audited.
    std::string_view configuration_descriptor;

    [[nodiscard]] contract::MethodIdentity contract_identity() const;

    friend bool operator==(const P18ReferenceMethodIdentity &,
                           const P18ReferenceMethodIdentity &) = default;
};

[[nodiscard]] const std::array<P18ReferenceMethodIdentity, kP18ReferenceMethodCount> &
p18_reference_method_identities_v1();

[[nodiscard]] const P18ReferenceMethodIdentity &
p18_reference_method_identity(P18ReferenceMethod method);

} // namespace engine_sim_offline::reference
