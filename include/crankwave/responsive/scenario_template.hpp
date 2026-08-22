#pragma once

#include "crankwave/authoring/engine_document.hpp"
#include "crankwave/authoring/scenario_document.hpp"
#include "crankwave/contract/common.hpp"
#include "crankwave/responsive/profile.hpp"

#include <variant>

namespace crankwave::responsive {

inline constexpr std::string_view kResponsiveScenarioTemplateIdentityMethodId =
    "crankwave.responsive-scenario-template.v1";

struct ResponsiveScenarioTemplate {
    authoring::ScenarioDocument document;
    contract::Sha256Digest identity_sha256;

    friend bool operator==(const ResponsiveScenarioTemplate &,
                           const ResponsiveScenarioTemplate &) = default;
};

using ResponsiveScenarioTemplateResult =
    std::variant<ResponsiveScenarioTemplate, contract::ValidationReport>;

// Builds the same trusted, fixed-environment template formerly materialized by
// the Node coordinator. No sibling scenario file or JavaScript runtime is needed.
[[nodiscard]] ResponsiveScenarioTemplateResult make_responsive_scenario_template(
    const authoring::EnginePackageDocument &engine,
    const ResponsiveBakeProfile &profile);

} // namespace crankwave::responsive
