#pragma once

#include "compile/resolved_authoring.hpp"

#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine_sim_offline::compile::detail {

// Methods outside the contract library are supplied by the executable integration
// layer. Absence means that the corresponding authored scenario capability is not
// available in that build.
struct ScenarioExecutionMethods {
    std::optional<contract::MethodIdentity> load_target_search;

    friend bool operator==(const ScenarioExecutionMethods &,
                           const ScenarioExecutionMethods &) = default;
};

struct ScenarioResolverLimits {
    // Prevent a short malformed request from asking the compiler to materialize an
    // effectively unbounded post-step RPM lane.
    std::uint64_t maximum_fixed_rate_trajectory_frames = UINT64_C(10000000);

    friend bool operator==(const ScenarioResolverLimits &,
                           const ScenarioResolverLimits &) = default;
};

struct ScenarioResolverContext {
    const contract::EngineSpec &engine;
    const contract::PresentationCalibration &presentation;
    const contract::ResolvedRandomnessPolicy &randomness;
    const contract::ProvenanceLedger &engine_provenance;
    std::span<const ResolvedFuelDescriptor> fuels;
    std::span<const ResolvedAudioBusDescriptor> audio_buses;
    ScenarioExecutionMethods execution_methods;
    contract::DistributionIntent distribution =
        contract::DistributionIntent::local_evaluation;
    ScenarioResolverLimits limits;
};

// Values not represented by the current RenderScenario contract remain explicit
// deterministic request inputs. Nothing in an accepted authored document is silently
// discarded.
struct ScenarioRequestInputMaterial {
    std::uint32_t telemetry_capacity_frames = 0;
    double authored_initial_engine_speed_rpm = 0.0;
    std::uint64_t total_physics_frames = 0;
    std::uint64_t audible_delivery_frames = 0;
    // Canonically ordered buses retain their authored route reduction order and
    // gain; the SourceMatrixContract intentionally describes ownership rather than
    // the executable mix graph.
    std::vector<ResolvedAudioBusDescriptor> selected_audio_buses;
    std::vector<contract::RouteId> rendered_route_ids;

    friend bool operator==(const ScenarioRequestInputMaterial &,
                           const ScenarioRequestInputMaterial &) = default;
};

struct ResolvedScenarioContracts {
    contract::RenderScenario scenario;
    contract::SourceMatrixContract source_matrix;
    contract::RandomPlan random_plan;
    contract::ProvenanceLedger combined_provenance;
    ScenarioRequestInputMaterial request_input;
    std::vector<StableIdAssignment> stable_id_assignments;

    friend bool operator==(const ResolvedScenarioContracts &,
                           const ResolvedScenarioContracts &) = default;
};

using ScenarioResolutionResult = CompileResult<ResolvedScenarioContracts>;

// The exact method admitted by the current fixed-rate kinematic scheduler.
[[nodiscard]] const contract::MethodIdentity &
fixed_rate_post_step_rpm_method_identity();

[[nodiscard]] ScenarioResolutionResult
resolve_scenario_document(const authoring::ScenarioDocument &document,
                          const ScenarioResolverContext &context) noexcept;

} // namespace engine_sim_offline::compile::detail
