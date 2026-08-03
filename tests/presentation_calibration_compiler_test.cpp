#include "presentation/presentation_calibration_compiler.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using namespace engine_sim_offline;

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] contract::Sha256Digest digest(std::uint8_t first) {
    contract::Sha256Digest result;
    result.bytes.front() = first;
    return result;
}

struct ResolutionBuilder {
    contract::ProvenanceLedger provenance{
        "engine-sim-offline.provenance.v1",
        {"neutral-presentation-inputs-v1", digest(1)},
        {
            {"asset-one-source", "assets/ir-one.wav", std::nullopt, digest(11),
             contract::RightsDisposition::permitted},
            {"asset-two-source", "assets/ir-two.wav", std::nullopt, digest(12),
             contract::RightsDisposition::permitted},
            {"unused-asset-source", "assets/ir-unused.wav", std::nullopt, digest(13),
             contract::RightsDisposition::permitted},
        },
        {
            {"claim", contract::ProvenanceOrigin::artistic, {}, std::nullopt},
        },
        {},
    };
    std::uint32_t next_resolution = 1;

    template <class T>
    [[nodiscard]] contract::ResolvedValue<T> resolved(T value, std::string path) {
        const auto id = "resolution-" + std::to_string(next_resolution++);
        provenance.resolutions.push_back({
            id,
            path,
            contract::ResolutionMode::authored,
            "claim",
            std::nullopt,
            {},
        });
        return {std::move(value), id};
    }
};

[[nodiscard]] contract::EngineSpec make_engine() {
    contract::EngineSpec engine;
    engine.profile_id.value = "neutral-engine-profile";
    engine.routes = {
        {
            contract::RouteId{1},
            {std::string{"route.one"}, {}},
            {contract::SourceRouteKind::exhaust_outlet, {}},
            std::nullopt,
            std::nullopt,
            std::nullopt,
        },
        {
            contract::RouteId{2},
            {std::string{"route.two"}, {}},
            {contract::SourceRouteKind::exhaust_outlet, {}},
            std::nullopt,
            std::nullopt,
            std::nullopt,
        },
    };
    return engine;
}

[[nodiscard]] contract::AudioAssetSpec
make_asset(ResolutionBuilder &builder, std::uint32_t id, std::string semantic_id,
           std::string evidence_id, std::uint8_t digest_byte) {
    const auto root = "presentation.assets." + semantic_id;
    return {
        contract::AudioAssetId{id},
        builder.resolved(std::move(semantic_id), root + ".semantic_id"),
        builder.resolved(std::move(evidence_id), root + ".evidence_source_id"),
        builder.resolved(digest(digest_byte), root + ".content_sha256"),
        builder.resolved(
            contract::AudioMediaContract{
                contract::AudioSampleEncoding::pcm_s16le,
                contract::AudioChannelLayout::mono,
                {44100, 1},
                128,
            },
            root + ".media"),
    };
}

[[nodiscard]] contract::PresentationCalibration
make_calibration(ResolutionBuilder &builder, const contract::EngineSpec &engine) {
    contract::PresentationCalibration calibration;
    calibration.schema_version = 2;
    calibration.calibration_id = "neutral-presentation-v1";
    calibration.engine_profile_id =
        builder.resolved(engine.profile_id.value, "presentation.engine_profile_id");
    const auto &methods = presentation::implemented_presentation_method_identities();
    calibration.methods = {
        builder.resolved(methods.reconstruction, "presentation.methods.reconstruction"),
        builder.resolved(methods.conditioning, "presentation.methods.conditioning"),
        builder.resolved(methods.impulse_response_conversion,
                         "presentation.methods.impulse_response_conversion"),
        builder.resolved(methods.convolution, "presentation.methods.convolution"),
        builder.resolved(methods.publication, "presentation.methods.publication"),
        builder.resolved(methods.audition_mix, "presentation.methods.audition_mix"),
    };
    calibration.conditioning = {
        builder.resolved(0.5, "presentation.conditioning.jitter_scale"),
        builder.resolved(10000.0,
                         "presentation.conditioning.jitter_modulation_cutoff_hz"),
        builder.resolved(0.01, "presentation.conditioning.derivative_mix_01"),
        builder.resolved(1.0, "presentation.conditioning.air_noise_mix_01"),
        builder.resolved(2000.0, "presentation.conditioning.air_noise_cutoff_hz"),
    };
    calibration.assets = {
        make_asset(builder, 1, "ir.one", "asset-one-source", 11),
        make_asset(builder, 2, "ir.two", "asset-two-source", 12),
    };
    calibration.routes = {
        {
            contract::RouteId{2},
            contract::AudioAssetId{2},
            builder.resolved(
                0.002, "presentation.routes.route.two.impulse_response_gain_linear"),
            builder.resolved(0.5, "presentation.routes.route.two.wet_mix_01"),
        },
        {
            contract::RouteId{1},
            contract::AudioAssetId{1},
            builder.resolved(
                0.001, "presentation.routes.route.one.impulse_response_gain_linear"),
            builder.resolved(1.0, "presentation.routes.route.one.wet_mix_01"),
        },
    };
    calibration.publication.calibration_gain_linear =
        builder.resolved(0x1.0p-26, "presentation.publication.calibration_gain_linear");
    calibration.audition = {
        builder.resolved(
            std::vector<contract::RouteId>{contract::RouteId{2}, contract::RouteId{1}},
            "presentation.audition.selected_routes"),
        builder.resolved(0.75, "presentation.audition.monitoring_gain_linear"),
        builder.resolved(0.02, "presentation.audition.fade_in_duration_s"),
        builder.resolved(0.02, "presentation.audition.fade_out_duration_s"),
    };
    calibration.provenance_schema_id = builder.provenance.schema_id;
    return calibration;
}

[[nodiscard]] contract::RenderScenario
make_scenario(const contract::EngineSpec &engine) {
    contract::RenderScenario scenario;
    scenario.engine_profile_id = engine.profile_id.value;
    scenario.total_duration_s.value = 17.0;
    scenario.audible_start_s.value = 2.0;
    scenario.audible_duration_s.value = 15.0;
    scenario.rates = {
        {10000, 1}, {10000, 1}, {192000, 1}, {192000, 1}, {192000, 1},
    };
    scenario.quality.value.capture_block_capacity_frames = 256;
    return scenario;
}

struct Inputs {
    ResolutionBuilder builder;
    contract::EngineSpec engine;
    contract::PresentationCalibration calibration;
    contract::RenderScenario scenario;

    Inputs()
        : engine(make_engine()), calibration(make_calibration(builder, engine)),
          scenario(make_scenario(engine)) {}
};

void append_third_route(Inputs &inputs) {
    inputs.engine.routes.push_back({
        contract::RouteId{3},
        {std::string{"route.three"}, {}},
        {contract::SourceRouteKind::exhaust_outlet, {}},
        std::nullopt,
        std::nullopt,
        std::nullopt,
    });
    inputs.calibration.assets.push_back(
        make_asset(inputs.builder, 3, "ir.three", "unused-asset-source", 13));
    inputs.calibration.routes.push_back({
        contract::RouteId{3},
        contract::AudioAssetId{3},
        inputs.builder.resolved(
            0.003, "presentation.routes.route.three.impulse_response_gain_linear"),
        inputs.builder.resolved(0.25, "presentation.routes.route.three.wet_mix_01"),
    });
    inputs.calibration.audition.selected_routes.value = {
        contract::RouteId{2},
        contract::RouteId{3},
        contract::RouteId{1},
    };
}

[[nodiscard]] const presentation::AdmittedPresentationCalibration &
expect_admitted(const presentation::PresentationCalibrationCompileResult &result) {
    const auto *admitted =
        std::get_if<presentation::AdmittedPresentationCalibration>(&result);
    expect(admitted != nullptr, "valid presentation calibration was rejected");
    return *admitted;
}

[[nodiscard]] const presentation::PresentationCalibrationCompileError &
expect_rejected(const presentation::PresentationCalibrationCompileResult &result,
                std::string_view path) {
    const auto *error =
        std::get_if<presentation::PresentationCalibrationCompileError>(&result);
    expect(error != nullptr &&
               error->code == presentation::PresentationCalibrationCompileErrorCode::
                                  invalid_resolved_calibration &&
               !error->validation.ok(),
           "invalid presentation calibration did not return a typed report");
    const bool found =
        std::ranges::any_of(error->validation.issues,
                            [path](const auto &issue) { return issue.path == path; });
    if (!found) {
        std::string actual_paths;
        for (const auto &issue : error->validation.issues) {
            if (!actual_paths.empty()) {
                actual_paths += ", ";
            }
            actual_paths += issue.path;
        }
        throw std::runtime_error{
            "presentation rejection did not identify expected path " +
            std::string{path} + "; actual paths: " + actual_paths};
    }
    return *error;
}

template <class Mutation>
void expect_mutation_rejected(Mutation mutation, std::string_view path) {
    Inputs inputs;
    mutation(inputs);
    static_cast<void>(expect_rejected(presentation::compile_presentation_calibration(
                                          inputs.calibration, inputs.engine,
                                          inputs.scenario, inputs.builder.provenance),
                                      path));
}

contract::ResolvedValue<contract::MethodIdentity> &
method_at(contract::PresentationMethods &methods, std::size_t index) {
    switch (index) {
    case 0:
        return methods.reconstruction;
    case 1:
        return methods.conditioning;
    case 2:
        return methods.impulse_response_conversion;
    case 3:
        return methods.convolution;
    case 4:
        return methods.publication;
    case 5:
        return methods.audition_mix;
    default:
        throw std::out_of_range{"method index is outside the presentation set"};
    }
}

void test_valid_projection_and_engine_route_order() {
    static_assert(!std::is_default_constructible_v<
                  presentation::AdmittedPresentationCalibration>);
    static_assert(
        !std::is_copy_constructible_v<presentation::AdmittedPresentationCalibration>);
    static_assert(
        std::is_move_constructible_v<presentation::AdmittedPresentationCalibration>);

    Inputs inputs;
    const auto result = presentation::compile_presentation_calibration(
        inputs.calibration, inputs.engine, inputs.scenario, inputs.builder.provenance);
    const auto &admitted = expect_admitted(result);
    expect(admitted.methods() ==
                   presentation::implemented_presentation_method_identities() &&
               admitted.conditioning() ==
                   presentation::RouteConditioningCalibration{0.5, 10000.0, 0.01, 1.0,
                                                              2000.0} &&
               admitted.capture_rate() == contract::RationalRateHz{10000, 1} &&
               admitted.capture_frames_per_block() == 200 &&
               admitted.total_block_count() == 850 &&
               admitted.pre_audible_block_count() == 100,
           "admitted method, conditioning, or block projection changed");
    expect(admitted.routes()[0].route_id() == contract::RouteId{1} &&
               admitted.routes()[0].impulse_response_asset_id() ==
                   contract::AudioAssetId{1} &&
               admitted.routes()[0].impulse_response_gain_linear() ==
                   inputs.calibration.routes[1].impulse_response_gain_linear &&
               admitted.routes()[0].wet_mix_01() == 1.0 &&
               admitted.routes()[1].route_id() == contract::RouteId{2} &&
               admitted.routes()[1].impulse_response_asset_id() ==
                   contract::AudioAssetId{2} &&
               admitted.routes()[1].wet_mix_01() == 0.5,
           "admitted routes did not follow engine excitation order");
    const std::array audition_routes{contract::RouteId{2}, contract::RouteId{1}};
    expect(std::ranges::equal(admitted.audition_route_ids(), audition_routes) &&
               admitted.publication_calibration_gain_linear() ==
                   inputs.calibration.publication.calibration_gain_linear &&
               admitted.mastering().audible_frame_count() == 2880000 &&
               admitted.mastering().fade_in_frame_count() == 3840 &&
               admitted.mastering().fade_out_frame_count() == 3840 &&
               admitted.mastering().monitoring_gain_linear() == 0.75F,
           "admitted publication or mastering projection changed");
}

void test_higher_capture_clock_preserves_the_20ms_timeline() {
    Inputs inputs;
    inputs.scenario.rates.physics = {20000, 1};
    inputs.scenario.rates.capture = {20000, 1};
    inputs.scenario.quality.value.capture_block_capacity_frames = 400;
    const auto result = presentation::compile_presentation_calibration(
        inputs.calibration, inputs.engine, inputs.scenario, inputs.builder.provenance);
    const auto &admitted = expect_admitted(result);
    expect(admitted.capture_rate() == contract::RationalRateHz{20000, 1} &&
               admitted.capture_frames_per_block() == 400 &&
               admitted.source_frames_per_block == 3840 &&
               admitted.total_block_count() == 850 &&
               admitted.pre_audible_block_count() == 100 &&
               admitted.mastering().audible_frame_count() == 2880000,
           "20 kHz capture did not preserve the exact 20 ms presentation timeline");
}

void test_dynamic_route_projection() {
    Inputs inputs;
    append_third_route(inputs);
    const auto result = presentation::compile_presentation_calibration(
        inputs.calibration, inputs.engine, inputs.scenario, inputs.builder.provenance);
    const auto &admitted = expect_admitted(result);
    const std::array audition_routes{contract::RouteId{2}, contract::RouteId{3},
                                     contract::RouteId{1}};
    expect(admitted.route_count() == 3 && admitted.routes().size() == 3 &&
               admitted.routes()[0].route_id() == contract::RouteId{1} &&
               admitted.routes()[1].route_id() == contract::RouteId{2} &&
               admitted.routes()[2].route_id() == contract::RouteId{3} &&
               admitted.routes()[2].impulse_response_asset_id() ==
                   contract::AudioAssetId{3} &&
               admitted.routes()[2].wet_mix_01() == 0.25 &&
               std::ranges::equal(admitted.audition_route_ids(), audition_routes),
           "three-route calibration was not admitted in engine and audition order");
}

void test_every_method_is_exact() {
    constexpr std::array paths{
        "presentation.methods.reconstruction.value",
        "presentation.methods.conditioning.value",
        "presentation.methods.impulse_response_conversion.value",
        "presentation.methods.convolution.value",
        "presentation.methods.publication.value",
        "presentation.methods.audition_mix.value",
    };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        expect_mutation_rejected(
            [index](Inputs &inputs) {
                method_at(inputs.calibration.methods, index)
                    .value.configuration_sha256.bytes.front() ^= UINT8_C(0x80);
            },
            paths[index]);
    }
}

void test_calibration_leaf_boundaries() {
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.conditioning.jitter_scale.value = -0.0;
        },
        "presentation.conditioning.jitter_scale.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.conditioning.jitter_modulation_cutoff_hz.value = 96000.0;
        },
        "presentation.conditioning.jitter_modulation_cutoff_hz.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.conditioning.derivative_mix_01.value = 1.01;
        },
        "presentation.conditioning.derivative_mix_01.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.conditioning.air_noise_mix_01.value =
                std::numeric_limits<double>::quiet_NaN();
        },
        "presentation.conditioning.air_noise_mix_01.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.conditioning.air_noise_cutoff_hz.value = 0.0;
        },
        "presentation.conditioning.air_noise_cutoff_hz.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.routes[0].impulse_response_gain_linear.value = -0.0;
        },
        "presentation.routes[0].impulse_response_gain_linear.value");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.calibration.routes[0].wet_mix_01.value = -0.0; },
        "presentation.routes[0].wet_mix_01.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.publication.calibration_gain_linear.value = 0.0;
        },
        "presentation.publication.calibration_gain_linear.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.monitoring_gain_linear.value =
                std::numeric_limits<double>::denorm_min();
        },
        "presentation.audition.monitoring_gain_linear.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.monitoring_gain_linear.value =
                std::numeric_limits<double>::max();
        },
        "presentation.audition.monitoring_gain_linear.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.fade_in_duration_s.value = -0.0;
        },
        "presentation.audition.fade_in_duration_s.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.fade_out_duration_s.value = 0.000001;
        },
        "presentation.audition.fade_out_duration_s.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.fade_in_duration_s.value = 8.0;
            inputs.calibration.audition.fade_out_duration_s.value = 8.0;
        },
        "presentation.audition");
}

void test_clock_and_topology_boundaries() {
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.rates.physics = {20000, 1}; },
        "scenario.rates.physics");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.rates.capture = {40000, 1}; },
        "scenario.rates.capture");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.rates.source_processing = {96000, 1}; },
        "scenario.rates.source_processing");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.rates.acoustic = {96000, 1}; },
        "scenario.rates.acoustic");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.rates.delivery = {96000, 1}; },
        "scenario.rates.delivery");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.total_duration_s.value = 17.0001; },
        "scenario.total_duration_s.value");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.audible_start_s.value = 2.0001; },
        "scenario.audible_start_s.value");
    expect_mutation_rejected(
        [](Inputs &inputs) { inputs.scenario.audible_duration_s.value = 14.0; },
        "scenario.audible_duration_s.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.scenario.total_duration_s.value = 6000.0;
            inputs.scenario.audible_duration_s.value = 5998.0;
        },
        "scenario.audible_duration_s.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.scenario.quality.value.capture_block_capacity_frames = 199;
        },
        "scenario.quality.value.capture_block_capacity_frames");
    expect_mutation_rejected([](Inputs &inputs) { inputs.engine.routes.pop_back(); },
                             "presentation.routes");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.engine.routes[1].kind.value =
                contract::SourceRouteKind::intake_inlet;
        },
        "engine.routes[1].kind.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.routes[0].route_id = contract::RouteId{1};
        },
        "presentation.routes.route.one.route_id");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.routes[0].route_id = contract::RouteId{3};
        },
        "engine.routes[1].id");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.routes[0].impulse_response_asset_id =
                contract::AudioAssetId{99};
        },
        "presentation.routes.route.two.impulse_response_asset_id");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.selected_routes.value.pop_back();
        },
        "presentation.audition.selected_routes.value");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.audition.selected_routes.value[1] = contract::RouteId{2};
        },
        "presentation.audition.selected_routes[1]");
    expect_mutation_rejected(
        [](Inputs &inputs) {
            inputs.calibration.assets.push_back(
                make_asset(inputs.builder, 3, "ir.unused", "unused-asset-source", 13));
        },
        "presentation.assets[2]");
}

void test_generic_validation_is_retained() {
    Inputs inputs;
    inputs.calibration.engine_profile_id.resolution_id.clear();
    const auto result = presentation::compile_presentation_calibration(
        inputs.calibration, inputs.engine, inputs.scenario, inputs.builder.provenance);
    const auto &error =
        expect_rejected(result, "presentation.engine_profile_id.resolution_id");
    expect(!error.validation.issues.empty(),
           "generic calibration validation report was discarded");
}

void test_schema_version_is_exact() {
    for (const auto schema_version : {UINT32_C(1), UINT32_C(3)}) {
        Inputs inputs;
        inputs.calibration.schema_version = schema_version;
        const auto result = presentation::compile_presentation_calibration(
            inputs.calibration, inputs.engine, inputs.scenario,
            inputs.builder.provenance);
        const auto &error = expect_rejected(result, "schema_version");
        const bool found =
            std::ranges::any_of(error.validation.issues, [](const auto &issue) {
                return issue.code == contract::ContractIssueCode::unsupported_value &&
                       issue.path == "schema_version";
            });
        expect(found, "unsupported presentation-calibration schema was not identified");
    }
}

void run_tests() {
    test_valid_projection_and_engine_route_order();
    test_higher_capture_clock_preserves_the_20ms_timeline();
    test_dynamic_route_projection();
    test_every_method_is_exact();
    test_calibration_leaf_boundaries();
    test_clock_and_topology_boundaries();
    test_generic_validation_is_retained();
    test_schema_version_is_exact();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "presentation calibration compiler test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
