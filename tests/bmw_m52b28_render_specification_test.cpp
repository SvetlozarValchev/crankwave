#include "engine_sim_offline/profiles/bmw_m52b28_inertial_dyno_listening_request.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_render_specification.hpp"

#include "presentation/presentation_method_registry.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] profiles::BmwM52b28InertialDynoListeningRequest make_request() {
    auto result = profiles::make_bmw_m52b28_inertial_dyno_listening_request();
    auto *request =
        std::get_if<profiles::BmwM52b28InertialDynoListeningRequest>(&result);
    if (request == nullptr) {
        throw std::runtime_error{
            "canonical BMW inertial-dyno request did not construct"};
    }
    return std::move(*request);
}

void test_canonical_render_specification() {
    auto request = make_request();
    const auto scenario = request.scenario;
    const auto simulation_resolution_count = request.provenance.resolutions.size();
    auto specification = profiles::make_bmw_m52b28_render_specification(
        std::move(request.engine), std::move(request.provenance));

    expect(contract::validate_render_admission(
               specification.engine, specification.presentation,
               specification.randomness, scenario, specification.provenance,
               specification.source_matrix)
               .ok(),
           "canonical BMW render specification failed complete admission");
    expect(specification.source_matrix ==
               contract::bmw_m52b28_exhaust_acoustic_source_matrix(),
           "BMW render specification selected a noncanonical source matrix");

    const auto &methods = presentation::implemented_presentation_method_identities();
    expect(
        specification.presentation.schema_version == 1U &&
            specification.presentation.methods.calibrated_pressure_publication.value ==
                methods.calibrated_pressure_publication &&
            specification.presentation.methods.coherent_two_outlet_audition.value ==
                methods.coherent_two_outlet_audition,
        "BMW render specification changed the exact presentation methods");
    expect(specification.presentation.monitoring.gain_linear.value == 0.5 &&
               specification.presentation.monitoring.fade_in_duration_s.value == 0.02 &&
               specification.presentation.monitoring.fade_out_duration_s.value == 0.02,
           "BMW render specification changed its common audition transform");
    expect(specification.randomness.seed_namespace_id.value ==
                   "baked.loaded_acceleration" &&
               specification.randomness.generator.value ==
                   contract::pcg32_generator_method_identity() &&
               specification.randomness.derivation.value ==
                   contract::component_seed_derivation_method_identity(),
           "BMW render specification changed deterministic combustion authority");

    expect(specification.provenance.bundle.id == "bmw-m52b28-render-provenance" &&
               !specification.provenance.bundle.sha256.is_zero(),
           "BMW render provenance bundle identity is incomplete");
    expect(specification.provenance.resolutions.size() ==
               simulation_resolution_count + 9U,
           "BMW render provenance did not add exactly its resolved leaves");
    expect(
        std::ranges::none_of(
            specification.provenance.resolutions,
            [](const contract::ResolutionRecord &resolution) {
                return resolution.id.starts_with("bmw-m52b28-render-resolution-") &&
                       (resolution.parameter_path.starts_with("presentation.assets") ||
                        resolution.parameter_path.find("reference") !=
                            std::string::npos);
            }),
        "BMW product render provenance retained an asset or reference path");

    const auto random_plan = contract::compile_random_plan(
        specification.randomness, specification.engine, scenario);
    expect(std::holds_alternative<contract::RandomPlan>(random_plan),
           "BMW render policy no longer reproduces the engine combustion streams");

    auto repeated_request = make_request();
    auto repeated = profiles::make_bmw_m52b28_render_specification(
        std::move(repeated_request.engine), std::move(repeated_request.provenance));
    expect(repeated == specification,
           "BMW render-specification factory is not byte-deterministic");
}

void test_factory_rejects_noncanonical_engine() {
    auto request = make_request();
    request.engine.profile_id.value = "different-engine-profile";
    bool rejected = false;
    try {
        static_cast<void>(profiles::make_bmw_m52b28_render_specification(
            std::move(request.engine), std::move(request.provenance)));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    expect(rejected, "BMW render factory accepted a different engine profile");
}

void run_tests() {
    test_canonical_render_specification();
    test_factory_rejects_noncanonical_engine();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
