#pragma once

#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine_sim_offline::contract {

struct RenderScenario;

// The only two presentation transforms in the product path. Acoustic simulation
// already produces the two physical outlet pressures at the delivery rate; there is
// no reconstruction, synthetic conditioning, asset convolution, or route selection
// hidden behind this contract.
struct AuthoredPresentationMethods {
    AuthoredValue<MethodSelection> calibrated_pressure_publication;
    AuthoredValue<MethodSelection> coherent_two_outlet_audition;

    friend bool operator==(const AuthoredPresentationMethods &,
                           const AuthoredPresentationMethods &) = default;
};

struct PresentationMethods {
    ResolvedValue<MethodIdentity> calibrated_pressure_publication;
    ResolvedValue<MethodIdentity> coherent_two_outlet_audition;

    friend bool operator==(const PresentationMethods &,
                           const PresentationMethods &) = default;
};

// One audition-only gain follows the coherent outlet sum. Float32 pressure
// publication has no independently tunable gain: its Pa/full-scale calibration is
// owned by the engine's physical exhaust-acoustic assembly.
struct AuthoredPresentationMonitoring {
    AuthoredValue<double> gain_linear;
    AuthoredValue<double> fade_in_duration_s;
    AuthoredValue<double> fade_out_duration_s;

    friend bool operator==(const AuthoredPresentationMonitoring &,
                           const AuthoredPresentationMonitoring &) = default;
};

struct PresentationMonitoring {
    ResolvedValue<double> gain_linear;
    ResolvedValue<double> fade_in_duration_s;
    ResolvedValue<double> fade_out_duration_s;

    friend bool operator==(const PresentationMonitoring &,
                           const PresentationMonitoring &) = default;
};

struct AuthoredPresentationCalibration {
    std::uint32_t schema_version = 0;
    std::string calibration_id;
    AuthoredValue<std::string> engine_profile_id;
    AuthoredPresentationMethods methods;
    AuthoredPresentationMonitoring monitoring;
    ProvenanceLedger provenance;

    friend bool operator==(const AuthoredPresentationCalibration &,
                           const AuthoredPresentationCalibration &) = default;
};

struct PresentationCalibration {
    std::uint32_t schema_version = 0;
    std::string calibration_id;
    ResolvedValue<std::string> engine_profile_id;
    PresentationMethods methods;
    PresentationMonitoring monitoring;
    std::string provenance_schema_id;

    friend bool operator==(const PresentationCalibration &,
                           const PresentationCalibration &) = default;
};

struct PresentationSourceRouteContext {
    RouteId route_id;
    std::string semantic_id;
    SourceRouteKind kind = SourceRouteKind::unspecified;

    friend bool operator==(const PresentationSourceRouteContext &,
                           const PresentationSourceRouteContext &) = default;
};

struct PresentationValidationContext {
    std::string engine_profile_id;
    std::vector<PresentationSourceRouteContext> routes;
    RenderRates rates;
    double audible_duration_s = 0.0;

    friend bool operator==(const PresentationValidationContext &,
                           const PresentationValidationContext &) = default;
};

[[nodiscard]] ValidationReport
validate(const AuthoredPresentationCalibration &calibration);
[[nodiscard]] ValidationReport validate(const PresentationCalibration &calibration,
                                        const EngineSpec &engine,
                                        const RenderScenario &scenario,
                                        const ProvenanceLedger &provenance);
[[nodiscard]] ValidationReport validate(const PresentationCalibration &calibration,
                                        const PresentationValidationContext &context,
                                        const ProvenanceLedger &provenance);

} // namespace engine_sim_offline::contract
