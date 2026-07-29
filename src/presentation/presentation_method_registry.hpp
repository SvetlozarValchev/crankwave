#pragma once

#include "engine_sim_offline/contract/presentation.hpp"

#include <cstdint>
#include <string_view>

namespace engine_sim_offline::presentation {

inline constexpr std::string_view kCalibratedPressurePublicationMethodId =
    "calibrated-two-outlet-pressure-float32-wave-publication-v1";
inline constexpr std::uint32_t kCalibratedPressurePublicationMethodVersion = 1;

inline constexpr std::string_view kCoherentTwoOutletAuditionMethodId =
    "coherent-two-outlet-quarter-sine-pcm24-wave-audition-v1";
inline constexpr std::uint32_t kCoherentTwoOutletAuditionMethodVersion = 1;

struct PresentationMethodIdentities {
    contract::MethodIdentity calibrated_pressure_publication;
    contract::MethodIdentity coherent_two_outlet_audition;

    friend bool operator==(const PresentationMethodIdentities &,
                           const PresentationMethodIdentities &) = default;
};

[[nodiscard]] std::string_view
calibrated_pressure_publication_method_descriptor() noexcept;
[[nodiscard]] std::string_view
coherent_two_outlet_audition_method_descriptor() noexcept;

[[nodiscard]] const contract::MethodIdentity &
calibrated_pressure_publication_method_identity();
[[nodiscard]] const contract::MethodIdentity &
coherent_two_outlet_audition_method_identity();

[[nodiscard]] const PresentationMethodIdentities &
implemented_presentation_method_identities();

[[nodiscard]] bool exactly_matches_implemented_presentation_methods(
    const contract::PresentationMethods &methods);

[[nodiscard]] contract::ValidationReport
admit_implemented_presentation_methods(const contract::PresentationMethods &methods);

} // namespace engine_sim_offline::presentation
