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
        {},
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

[[nodiscard]] contract::EngineSpec make_engine() {
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
    return engine;
}

[[nodiscard]] contract::PresentationCalibration
make_presentation(ResolutionBuilder &builder, const contract::EngineSpec &engine) {
    contract::PresentationCalibration presentation;
    presentation.schema_version = 1;
    presentation.calibration_id = "test-presentation-v1";
    presentation.engine_profile_id =
        builder.resolved(engine.profile_id.value, "presentation.engine_profile_id");

    const auto &methods = presentation::implemented_presentation_method_identities();
    presentation.methods = {
        builder.resolved(methods.calibrated_pressure_publication,
                         "presentation.methods.calibrated_pressure_publication"),
        builder.resolved(methods.coherent_two_outlet_audition,
                         "presentation.methods.coherent_two_outlet_audition"),
    };
    presentation.monitoring = {
        builder.resolved(0.75, "presentation.monitoring.gain_linear"),
        builder.resolved(0.02, "presentation.monitoring.fade_in_duration_s"),
        builder.resolved(0.02, "presentation.monitoring.fade_out_duration_s"),
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

[[nodiscard]] contract::SourceMatrixContract make_source_matrix() {
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
            {"stem/route.two.pressure"},
        },
        {
            "route.one",
            contract::SourceRouteKind::exhaust_outlet,
            contract::RouteDisposition::rendered,
            "",
            {"stem/route.one.pressure"},
        },
    };
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
        {"stem/route.two.pressure", contract::ArtifactKind::audio, float_audio, false},
        {"master/engine.raw", contract::ArtifactKind::audio, float_audio, false},
        {"stem/route.one.pressure", contract::ArtifactKind::audio, float_audio, false},
    };
    return matrix;
}

struct ProjectionFixture {
    ResolutionBuilder builder;
    contract::RenderRequestRecord request;

    ProjectionFixture() : request(make_request(builder)) {}

  private:
    [[nodiscard]] static contract::RenderRequestRecord
    make_request(ResolutionBuilder &builder) {
        auto engine = make_engine();
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
            make_source_matrix(),
        };
    }
};

template <class Job>
concept LvalueExecutable = requires(Job &job, RenderSink &sink) { job.execute(sink); };

template <class Job>
concept RvalueExecutable =
    requires(Job &&job, RenderSink &sink) { std::move(job).execute(sink); };

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
    auto &audition_method =
        inputs.presentation.methods.coherent_two_outlet_audition.value;
    audition_method.id = "audition-method";
    audition_method.version = 42;
    for (std::size_t index = 0;
         index < audition_method.configuration_sha256.bytes.size();
         ++index) {
        audition_method.configuration_sha256.bytes[index] =
            static_cast<std::uint8_t>(index);
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
               error->path == "presentation.monitoring.metadata",
           "oversized audition metadata escaped its fixed bound");
}

void test_complete_projection() {
    ProjectionFixture fixture;
    auto result = derive_render_job_projection(
        fixture.request, std::array{contract::RouteId{1}, contract::RouteId{2}});
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

    const auto &route_0 = projection->outlet_pressure_artifacts[0];
    const auto &route_1 = projection->outlet_pressure_artifacts[1];
    expect(route_0.role == "stem/route.one.pressure" &&
               route_0.relative_path == "audio/stem%2froute.one.pressure.wav" &&
               route_1.role == "stem/route.two.pressure" &&
               route_1.relative_path == "audio/stem%2froute.two.pressure.wav",
           "outlet pressure artifact positions or derived paths changed");
    expect(!route_0.diagnostic && !route_1.diagnostic,
           "physical outlet pressure stems unexpectedly became diagnostic");
    expect(projection->raw_master_artifact.role == "master/engine.raw" &&
               projection->raw_master_artifact.relative_path ==
                   "audio/master%2fengine.raw.wav" &&
               projection->audition_master_artifact.role == "master/engine.audition" &&
               projection->audition_master_artifact.audio.has_value() &&
               projection->audition_master_artifact.audio->sample_encoding_id ==
                   "pcm_s24le",
           "master bus selection inferred order instead of bus kind");

    const auto &method =
        fixture.request.resolved_inputs.presentation.methods
            .coherent_two_outlet_audition.value;
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

void run_tests() {
    test_opaque_job_shape();
    test_artifact_path_projection();
    test_audition_metadata_projection();
    test_complete_projection();
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
