#include "presentation/presentation_calibration_compiler.hpp"
#include "presentation/presentation_method_registry.hpp"
#include "render/compiled_presentation_job.hpp"
#include "render/render_job_derivation.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::render_detail;

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

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(value.bytes.size() * 2);
    for (const auto byte : value.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

struct ResolutionBuilder {
    contract::ProvenanceLedger provenance{
        "engine-sim-offline.provenance.v1",
        {"render-job-derivation-inputs-v1", digest(1)},
        {
            {
                "configured-ir-source",
                "assets/configured-ir.wav",
                std::nullopt,
                digest(11),
                contract::RightsDisposition::permitted,
            },
        },
        {
            {
                "claim",
                contract::ProvenanceOrigin::artistic,
                {},
                std::nullopt,
            },
        },
        {},
    };
    std::uint32_t next_resolution = 1;

    template <class T>
    [[nodiscard]] contract::ResolvedValue<T> resolved(T value, std::string path) {
        const auto id = "resolution-" + std::to_string(next_resolution++);
        provenance.resolutions.push_back({
            id,
            std::move(path),
            contract::ResolutionMode::authored,
            "claim",
            std::nullopt,
            {},
        });
        return {std::move(value), id};
    }
};

[[nodiscard]] contract::EngineSpec make_engine(std::size_t route_count = 2) {
    contract::EngineSpec engine;
    engine.id = contract::EngineId{7};
    engine.engine_id.value = "test-engine";
    engine.profile_id.value = "test-engine-profile";
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
    if (route_count == 3) {
        engine.routes.push_back({
            contract::RouteId{3},
            {std::string{"route.three"}, {}},
            {contract::SourceRouteKind::exhaust_outlet, {}},
            std::nullopt,
            std::nullopt,
            std::nullopt,
        });
    }
    return engine;
}

[[nodiscard]] contract::PresentationCalibration
make_presentation(ResolutionBuilder &builder, const contract::EngineSpec &engine) {
    contract::PresentationCalibration presentation;
    presentation.schema_version = 2;
    presentation.calibration_id = "test-presentation-v1";
    presentation.engine_profile_id =
        builder.resolved(engine.profile_id.value, "presentation.engine_profile_id");

    const auto &methods = presentation::implemented_presentation_method_identities();
    presentation.methods = {
        builder.resolved(methods.reconstruction, "presentation.methods.reconstruction"),
        builder.resolved(methods.conditioning, "presentation.methods.conditioning"),
        builder.resolved(methods.impulse_response_conversion,
                         "presentation.methods.impulse_response_conversion"),
        builder.resolved(methods.convolution, "presentation.methods.convolution"),
        builder.resolved(methods.publication, "presentation.methods.publication"),
        builder.resolved(methods.audition_mix, "presentation.methods.audition_mix"),
    };
    presentation.conditioning = {
        builder.resolved(0.5, "presentation.conditioning.jitter_scale"),
        builder.resolved(10000.0,
                         "presentation.conditioning.jitter_modulation_cutoff_hz"),
        builder.resolved(0.01, "presentation.conditioning.derivative_mix_01"),
        builder.resolved(1.0, "presentation.conditioning.air_noise_mix_01"),
        builder.resolved(2000.0, "presentation.conditioning.air_noise_cutoff_hz"),
    };
    presentation.assets = {
        {
            contract::AudioAssetId{1},
            builder.resolved(std::string{"configured-ir"},
                             "presentation.assets.configured-ir.semantic_id"),
            builder.resolved(std::string{"configured-ir-source"},
                             "presentation.assets.configured-ir.evidence_source_id"),
            builder.resolved(digest(11),
                             "presentation.assets.configured-ir.content_sha256"),
            builder.resolved(
                contract::AudioMediaContract{
                    contract::AudioSampleEncoding::pcm_s16le,
                    contract::AudioChannelLayout::mono,
                    {44100, 1},
                    128,
                },
                "presentation.assets.configured-ir.media"),
        },
    };
    presentation.routes = {
        {
            contract::RouteId{2},
            contract::AudioAssetId{1},
            builder.resolved(
                0.001, "presentation.routes.route.two.impulse_response_gain_linear"),
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
    if (engine.routes.size() == 3) {
        presentation.routes.push_back({
            contract::RouteId{3},
            contract::AudioAssetId{1},
            builder.resolved(
                0.001, "presentation.routes.route.three.impulse_response_gain_linear"),
            builder.resolved(0.25, "presentation.routes.route.three.wet_mix_01"),
        });
    }
    presentation.publication.calibration_gain_linear =
        builder.resolved(0x1.0p-26, "presentation.publication.calibration_gain_linear");
    std::vector<contract::RouteId> audition_routes{contract::RouteId{2},
                                                   contract::RouteId{1}};
    if (engine.routes.size() == 3) {
        audition_routes = {contract::RouteId{2}, contract::RouteId{3},
                           contract::RouteId{1}};
    }
    presentation.audition = {
        builder.resolved(std::move(audition_routes),
                         "presentation.audition.selected_routes"),
        builder.resolved(0.75, "presentation.audition.monitoring_gain_linear"),
        builder.resolved(0.02, "presentation.audition.fade_in_duration_s"),
        builder.resolved(0.02, "presentation.audition.fade_out_duration_s"),
    };
    presentation.provenance_schema_id = builder.provenance.schema_id;
    return presentation;
}

[[nodiscard]] contract::RenderScenario
make_scenario(const contract::EngineSpec &engine) {
    contract::RenderScenario scenario;
    scenario.scenario_id = "test-dyno-pull";
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

[[nodiscard]] contract::SourceMatrixContract
make_source_matrix(std::size_t route_count = 2) {
    const contract::AudioContract float_audio{
        {192000, 1},
        2880000,
        "mono",
        "float32le",
    };
    const contract::AudioContract audition_audio{
        {192000, 1},
        2880000,
        "mono",
        "pcm_s24le",
    };

    contract::SourceMatrixContract matrix;
    matrix.id = "test-source-matrix-v1";
    matrix.sha256 = digest(40);
    matrix.distribution = contract::DistributionIntent::local_evaluation;
    // Deliberately opposite engine order: route records must still follow engine
    // order, while output-bus records preserve source-matrix order.
    matrix.required_source_routes = {
        {
            "route.two",
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {
                "stem/route.two.dry",
                "stem/route.two.configured_ir",
                "stem/route.two.selected",
            },
        },
        {
            "route.one",
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {
                "stem/route.one.dry",
                "stem/route.one.configured_ir",
                "stem/route.one.selected",
            },
        },
    };
    if (route_count == 3) {
        matrix.required_source_routes.insert(
            matrix.required_source_routes.begin() + 1,
            {
                "route.three",
                contract::SourceRouteKind::exhaust_outlet,
                contract::RouteDisposition::rendered,
                "",
                {
                    "stem/route.three.dry",
                    "stem/route.three.configured_ir",
                    "stem/route.three.selected",
                },
            });
    }
    matrix.required_output_buses = {
        {
            "master.engine.audition",
            contract::OutputBusKind::master_engine_audition,
            {"master/engine.audition"},
        },
        {
            "master.engine.raw",
            contract::OutputBusKind::master_engine_raw,
            {"master/engine.raw"},
        },
    };
    // Deliberately unrelated to signal topology order.
    matrix.required_artifacts = {
        {"master/engine.audition", contract::ArtifactKind::audio, audition_audio,
         false},
        {"stem/route.two.selected", contract::ArtifactKind::audio, float_audio, false},
        {"stem/route.one.configured_ir", contract::ArtifactKind::audio, float_audio,
         true},
        {"master/engine.raw", contract::ArtifactKind::audio, float_audio, false},
        {"stem/route.two.dry", contract::ArtifactKind::audio, float_audio, true},
        {"stem/route.one.selected", contract::ArtifactKind::audio, float_audio, false},
        {"stem/route.two.configured_ir", contract::ArtifactKind::audio, float_audio,
         true},
        {"stem/route.one.dry", contract::ArtifactKind::audio, float_audio, true},
    };
    if (route_count == 3) {
        matrix.required_artifacts.push_back(
            {"stem/route.three.dry", contract::ArtifactKind::audio, float_audio, true});
        matrix.required_artifacts.push_back({"stem/route.three.configured_ir",
                                             contract::ArtifactKind::audio, float_audio,
                                             true});
        matrix.required_artifacts.push_back({"stem/route.three.selected",
                                             contract::ArtifactKind::audio, float_audio,
                                             false});
    }
    return matrix;
}

struct ProjectionFixture {
    ResolutionBuilder builder;
    contract::RenderRequestRecord request;
    presentation::AdmittedPresentationCalibration calibration;

    explicit ProjectionFixture(std::size_t route_count = 2)
        : request(make_request(builder, route_count)),
          calibration(compile_calibration(request, builder.provenance)) {}

  private:
    [[nodiscard]] static contract::RenderRequestRecord
    make_request(ResolutionBuilder &builder, std::size_t route_count) {
        auto engine = make_engine(route_count);
        auto presentation = make_presentation(builder, engine);
        auto scenario = make_scenario(engine);
        return {
            {
                std::move(engine),
                std::move(presentation),
                {},
                std::move(scenario),
            },
            builder.provenance,
            make_source_matrix(route_count),
            {},
        };
    }

    [[nodiscard]] static presentation::AdmittedPresentationCalibration
    compile_calibration(const contract::RenderRequestRecord &request,
                        const contract::ProvenanceLedger &provenance) {
        auto result = presentation::compile_presentation_calibration(
            request.resolved_inputs.presentation, request.resolved_inputs.engine,
            request.resolved_inputs.scenario, provenance);
        if (!std::holds_alternative<presentation::AdmittedPresentationCalibration>(
                result)) {
            throw std::runtime_error{
                "valid render-job projection fixture failed calibration admission"};
        }
        return std::get<presentation::AdmittedPresentationCalibration>(
            std::move(result));
    }
};

template <class Job>
concept LvalueExecutable =
    requires(Job &job, RenderSink &sink, const RenderSpecification &specification,
             const contract::RenderScenario &scenario) {
        job.execute(sink, specification, scenario);
    };

template <class Job>
concept RvalueExecutable =
    requires(Job &&job, RenderSink &sink, const RenderSpecification &specification,
             const contract::RenderScenario &scenario) {
        std::move(job).execute(sink, specification, scenario);
    };

void test_opaque_job_shape() {
    using Job = CompiledPresentationJob;
    static_assert(!std::is_default_constructible_v<Job>);
    static_assert(!std::is_copy_constructible_v<Job>);
    static_assert(!std::is_copy_assignable_v<Job>);
    static_assert(std::is_nothrow_move_constructible_v<Job>);
    static_assert(!std::is_move_assignable_v<Job>);
    static_assert(std::is_destructible_v<Job>);
    static_assert(!LvalueExecutable<Job>);
    static_assert(RvalueExecutable<Job>);
    static_assert(
        std::is_same_v<CompiledPresentationJobResult,
                       std::variant<CompiledPresentationJob, contract::RenderFailure>>);
}

void test_artifact_path_projection() {
    {
        auto result = derive_audio_artifact_path("exhaust.reference.0.configured_ir");
        const auto *path = std::get_if<std::string>(&result);
        expect(path != nullptr &&
                   *path == "audio/exhaust.reference.0.configured_ir.wav",
               "plain artifact role projection changed");
    }
    {
        auto result = derive_audio_artifact_path("exhaust/reference/0.selected");
        const auto *path = std::get_if<std::string>(&result);
        expect(path != nullptr && *path == "audio/exhaust%2freference%2f0.selected.wav",
               "slash escaping is not exact lowercase %2f");
    }
    {
        const std::string largest_role(230, 'a');
        const auto result = derive_audio_artifact_path(largest_role);
        const auto *path = std::get_if<std::string>(&result);
        expect(path != nullptr && path->size() == 240,
               "largest portable artifact path was rejected");
    }
    {
        const std::string oversized_role(231, 'a');
        const auto result = derive_audio_artifact_path(oversized_role);
        const auto *error = std::get_if<RenderJobDerivationError>(&result);
        expect(error != nullptr &&
                   error->code ==
                       RenderJobDerivationErrorCode::artifact_path_too_long &&
                   error->path == "artifact.relative_path",
               "oversized artifact path did not return its typed rejection");
    }
    for (const std::string_view invalid : {"", "Uppercase", "percent%role"}) {
        const auto result = derive_audio_artifact_path(invalid);
        const auto *error = std::get_if<RenderJobDerivationError>(&result);
        expect(error != nullptr &&
                   error->code == RenderJobDerivationErrorCode::invalid_artifact_role,
               "invalid artifact role escaped path projection");
    }
}

void test_audition_metadata_projection() {
    contract::ResolvedRenderInputs inputs;
    inputs.engine.engine_id.value = "engine-id";
    inputs.engine.profile_id.value = "profile-id";
    inputs.presentation.calibration_id = "presentation-id";
    inputs.presentation.methods.audition_mix.value.id = "audition-method";
    inputs.presentation.methods.audition_mix.value.version = 42;
    for (std::size_t index = 0; index < inputs.presentation.methods.audition_mix.value
                                            .configuration_sha256.bytes.size();
         ++index) {
        inputs.presentation.methods.audition_mix.value.configuration_sha256
            .bytes[index] = static_cast<std::uint8_t>(index);
    }
    inputs.scenario.scenario_id = "scenario-id";

    contract::SourceMatrixContract matrix;
    matrix.id = "matrix-id";
    const auto result = derive_audition_metadata(inputs, matrix);
    const auto *metadata = std::get_if<artifacts::AuditionWaveMetadata>(&result);
    expect(metadata != nullptr &&
               metadata->comment ==
                   "engine=engine-id;profile=profile-id;scenario=scenario-id;"
                   "presentation=presentation-id;source_matrix=matrix-id" &&
               metadata->title == "engine=engine-id;scenario=scenario-id" &&
               metadata->software ==
                   "engine-sim-offline;method=audition-method;version=42;"
                   "configuration_sha256="
                   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
           "audition INFO metadata projection changed");

    inputs.engine.engine_id.value.assign(artifacts::kMaximumAuditionMetadataFieldBytes,
                                         'a');
    const auto oversized = derive_audition_metadata(inputs, matrix);
    const auto *error = std::get_if<RenderJobDerivationError>(&oversized);
    expect(error != nullptr &&
               error->code == RenderJobDerivationErrorCode::audition_metadata_invalid &&
               error->path == "presentation.audition.metadata",
           "oversized audition metadata escaped its fixed bound");
}

void test_complete_projection() {
    ProjectionFixture fixture;
    auto result = derive_render_job_projection(fixture.request, fixture.calibration);
    const auto *projection = std::get_if<RenderJobProjection>(&result);
    expect(projection != nullptr, "valid complete render-job projection was rejected");

    expect(projection->output_contract ==
               contract::resolve_output_contract(fixture.request.source_matrix),
           "job output contract is not the exact source-matrix projection");
    expect(projection->routes.size() == 2 &&
               projection->routes[0].route_id == contract::RouteId{1} &&
               projection->routes[0].semantic_id == "route.one" &&
               projection->routes[1].route_id == contract::RouteId{2} &&
               projection->routes[1].semantic_id == "route.two",
           "manifest routes did not follow engine execution order");
    expect(projection->output_buses.size() == 2 &&
               projection->output_buses[0].kind ==
                   contract::OutputBusKind::master_engine_audition &&
               projection->output_buses[1].kind ==
                   contract::OutputBusKind::master_engine_raw,
           "manifest output buses did not retain source-matrix order");

    const auto &route_0 = projection->route_artifacts[0];
    const auto &route_1 = projection->route_artifacts[1];
    expect(route_0.dry.role == "stem/route.one.dry" &&
               route_0.dry.relative_path == "audio/stem%2froute.one.dry.wav" &&
               route_0.configured_ir.role == "stem/route.one.configured_ir" &&
               route_0.selected.role == "stem/route.one.selected" &&
               route_1.dry.role == "stem/route.two.dry" &&
               route_1.configured_ir.role == "stem/route.two.configured_ir" &&
               route_1.selected.role == "stem/route.two.selected",
           "route artifact positions or derived paths changed");
    expect(route_0.dry.diagnostic && route_0.configured_ir.diagnostic &&
               !route_0.selected.diagnostic && route_1.dry.diagnostic &&
               route_1.configured_ir.diagnostic && !route_1.selected.diagnostic,
           "artifact diagnostic policy was not copied exactly");
    expect(projection->raw_master_artifact.role == "master/engine.raw" &&
               projection->raw_master_artifact.relative_path ==
                   "audio/master%2fengine.raw.wav" &&
               projection->audition_master_artifact.role == "master/engine.audition" &&
               projection->audition_master_artifact.audio.has_value() &&
               projection->audition_master_artifact.audio->sample_encoding_id ==
                   "pcm_s24le",
           "master bus selection inferred order instead of bus kind");

    const auto &method =
        fixture.request.resolved_inputs.presentation.methods.audition_mix.value;
    expect(projection->audition_metadata.comment ==
                   "engine=test-engine;profile=test-engine-profile;"
                   "scenario=test-dyno-pull;presentation=test-presentation-v1;"
                   "source_matrix=test-source-matrix-v1" &&
               projection->audition_metadata.title ==
                   "engine=test-engine;scenario=test-dyno-pull" &&
               projection->audition_metadata.software ==
                   "engine-sim-offline;method=" + method.id + ";version=" +
                       std::to_string(method.version) + ";configuration_sha256=" +
                       digest_hex(method.configuration_sha256),
           "complete projection did not retain exact audition metadata");
}

void test_dynamic_route_projection() {
    ProjectionFixture fixture{3};
    auto result = derive_render_job_projection(fixture.request, fixture.calibration);
    const auto *projection = std::get_if<RenderJobProjection>(&result);
    expect(projection != nullptr && projection->routes.size() == 3 &&
               projection->route_artifacts.size() == 3 &&
               projection->output_contract.required_artifacts.size() == 11 &&
               projection->routes[2].route_id == contract::RouteId{3} &&
               projection->routes[2].semantic_id == "route.three" &&
               projection->route_artifacts[2].dry.role == "stem/route.three.dry" &&
               projection->route_artifacts[2].configured_ir.role ==
                   "stem/route.three.configured_ir" &&
               projection->route_artifacts[2].selected.role ==
                   "stem/route.three.selected" &&
               projection->output_buses.size() == 2,
           "three-route job did not project 3*R+2 artifacts and two master buses");
}

void run_tests() {
    test_opaque_job_shape();
    test_artifact_path_projection();
    test_audition_metadata_projection();
    test_complete_projection();
    test_dynamic_route_projection();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "render job derivation test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
