#include "engine_sim_offline/artifacts/audio_atlas_manifest_encoder.hpp"
#include "engine_sim_offline/contract/audio_atlas.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
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

[[nodiscard]] contract::Sha256Digest digest(std::uint8_t first) {
    contract::Sha256Digest result;
    result.bytes.front() = first;
    return result;
}

[[nodiscard]] contract::AudioAtlasContentIdentity identity(std::string id,
                                                           std::uint8_t hash) {
    return {std::move(id), digest(hash)};
}

void append_artifact(contract::AudioAtlasManifest &manifest, std::string id,
                     contract::AudioAtlasArtifactEncoding encoding,
                     std::uint64_t element_count, std::uint8_t hash) {
    const auto bytes_per_element =
        encoding == contract::AudioAtlasArtifactEncoding::float32le ? 4U : 16U;
    manifest.artifacts.push_back(
        {std::move(id),
         "audio/" + std::to_string(hash) + ".bin",
         encoding,
         element_count,
         element_count * bytes_per_element,
         digest(hash)});
}

[[nodiscard]] contract::AudioAtlasManifest valid_manifest() {
    contract::AudioAtlasManifest manifest;
    manifest.id = "generic-responsive-atlas";
    manifest.engine = "generic-four-stroke-engine";
    manifest.public_seed = 42U;
    manifest.audio.sample_rate_hz = 192000U;
    manifest.audio.buses = {{"master.engine.audition"}};
    manifest.domain = {
        700.0,
        6500.0,
        contract::AudioAtlasLoadCoordinate::
            measured_intake_manifold_pressure_pa_abs,
        10000.0,
        102000.0,
        2.0,
        {3U},
        contract::AudioAtlasOutOfDomainBehavior::unavailable,
    };

    auto &texture = manifest.phase_texture;
    texture.samples_per_cycle = 8U;
    texture.residual_cycle_count = 2U;
    texture.residual_taper = {
        contract::AudioAtlasResidualTaperMethod::
            boundary_zero_smoothstep_v1,
        0.0,
        0.25,
        2U,
    };
    texture.rpm_anchors = {700.0, 6500.0};
    texture.load_lanes = {
        {"coast", 0.0, 3U},
        {"power", 1.0, 3U},
    };
    texture.reference_cell_id = "rpm700-coast";
    texture.source_route_ids = {"exhaust.front.dry", "exhaust.rear.dry"};

    std::uint8_t hash = 1U;
    const auto add_phase_cell = [&](std::string id, std::string lane, double rpm,
                                    double load, double throttle,
                                    double shift) {
        contract::AudioAtlasPhaseCell cell;
        cell.id = std::move(id);
        cell.load_lane_id = std::move(lane);
        cell.rpm = rpm;
        cell.load_coordinate_pa_abs = load;
        cell.requested_throttle_01 = throttle;
        cell.state_mask = 3U;
        cell.shift_to_canonical_samples = shift;
        for (const auto &route_id : texture.source_route_ids) {
            const auto slug = cell.id +
                              (route_id == "exhaust.front.dry" ? "-front"
                                                               : "-rear");
            const auto mean_id = slug + "-mean";
            const auto residual_id = slug + "-residual";
            append_artifact(manifest, mean_id,
                            contract::AudioAtlasArtifactEncoding::float32le,
                            8U, hash++);
            append_artifact(manifest, residual_id,
                            contract::AudioAtlasArtifactEncoding::float32le,
                            16U, hash++);
            cell.routes.push_back(
                {route_id, mean_id, residual_id, 0.01, 0.0001});
        }
        texture.cells.push_back(std::move(cell));
    };
    add_phase_cell("rpm700-coast", "coast", 700.0, 20000.0, 0.0, 0.0);
    add_phase_cell("rpm700-power", "power", 700.0, 100000.0, 1.0, 1.0);
    add_phase_cell("rpm6500-coast", "coast", 6500.0, 15000.0, 0.0, -2.0);
    add_phase_cell("rpm6500-power", "power", 6500.0, 101000.0, 1.0, -3.0);

    manifest.transient_policy = {
        contract::AudioAtlasTransientDetectionMethod::causal_throttle_window_v1,
        15360U,
        11520U,
        0.30,
        0.70,
        0.12,
        48000U,
        5760U,
        {5760U, 17280U, 63360U, 0.75},
        {3840U, 15360U, 80640U, 0.65},
    };

    const auto add_transient_layer =
        [&](std::string id,
            contract::AudioAtlasTransientDirection direction) {
            contract::AudioAtlasTransientLayer layer;
            layer.id = std::move(id);
            layer.direction = direction;
            layer.source_route_ids = texture.source_route_ids;
            contract::AudioAtlasTransientCell cell;
            cell.id = layer.id + "-cell";
            cell.rpm = 3000.0;
            cell.load_coordinate_pa_abs = 80000.0;
            cell.requested_throttle_01 =
                direction == contract::AudioAtlasTransientDirection::rising
                    ? 1.0
                    : 0.0;
            cell.state_mask = 3U;
            cell.cycle_count = 3U;
            cell.samples_per_cycle = 8U;
            cell.phase_origin_revolutions = 0.5;
            cell.source_cycle_origin_ordinal_mod_cycle_count = 1U;
            cell.seam_closure_frames_per_side = 1U;
            for (const auto &route_id : layer.source_route_ids) {
                const auto artifact_id =
                    cell.id +
                    (route_id == "exhaust.front.dry" ? "-front" : "-rear");
                append_artifact(
                    manifest, artifact_id,
                    contract::AudioAtlasArtifactEncoding::float32le, 24U,
                    hash++);
                cell.routes.push_back({route_id, artifact_id});
            }
            layer.cells.push_back(std::move(cell));
            manifest.transient_layers.push_back(std::move(layer));
        };
    add_transient_layer("throttle-rise",
                        contract::AudioAtlasTransientDirection::rising);
    add_transient_layer("throttle-fall",
                        contract::AudioAtlasTransientDirection::falling);

    append_artifact(manifest, "fixed-transfer-spectrum",
                    contract::AudioAtlasArtifactEncoding::complex_float64le,
                    8U, hash++);
    const auto renderer_build = identity("renderer-build", hash++);
    manifest.presentation.method_identity =
        identity("canonical-responsive-presentation", hash++);
    manifest.presentation.build_identity = renderer_build;
    manifest.presentation.batch_frames = 4U;
    manifest.presentation.captured_to_source_scale = 67108864.0;
    manifest.presentation.source_routes = {
        {"exhaust.front.dry",
         "master.engine.audition",
         0.75,
         {contract::AudioAtlasTransferKind::fixed_spectrum_convolution,
          8U,
          5U,
          "fixed-transfer-spectrum"}},
        {"exhaust.rear.dry",
         "master.engine.audition",
         0.75,
         {contract::AudioAtlasTransferKind::fixed_spectrum_convolution,
          8U,
          5U,
          "fixed-transfer-spectrum"}},
    };
    manifest.presentation.master = {
        contract::AudioAtlasMasterMethod::canonical_adaptive_v1, 1.0};
    manifest.provenance = {
        identity("generic-four-stroke-engine", hash++),
        identity("responsive-atlas-bake", hash++),
        renderer_build,
        {"responsive-atlas-source-inputs", digest(hash++)},
    };
    return manifest;
}

[[nodiscard]] bool has_issue(const contract::ValidationReport &report,
                             contract::ContractIssueCode code,
                             std::string_view path) {
    return std::ranges::any_of(report.issues, [&](const auto &issue) {
        return issue.code == code && issue.path == path;
    });
}

void test_responsive_manifest_is_admitted_and_encoded() {
    const auto manifest = valid_manifest();
    expect(contract::validate(manifest).ok(),
           "valid responsive audio atlas was rejected");

    const auto encoded = artifacts::encode_audio_atlas_manifest(manifest);
    const auto *bytes = std::get_if<artifacts::ManifestEncoding>(&encoded);
    expect(bytes != nullptr, "valid responsive audio atlas was not encoded");
    const auto json = std::string{
        reinterpret_cast<const char *>(bytes->bytes.data()), bytes->bytes.size()};
    expect(json.find(R"json("schema":"engine-sim-offline/audio-atlas")json") !=
               std::string::npos,
           "encoded atlas omitted the sole current schema");
    expect(json.find(R"json("phase_texture")json") != std::string::npos &&
               json.find(R"json("transient_layers")json") !=
                   std::string::npos &&
               json.find(R"json("captured_to_source_scale")json") !=
                   std::string::npos,
           "encoded atlas omitted responsive package fields");
    expect(json.find("moving_segments") == std::string::npos &&
               json.find("stationary_tiles") == std::string::npos &&
               json.find("publication_scale") == std::string::npos &&
               json.find("mix_gain_linear") == std::string::npos,
           "encoder leaked a replaced atlas representation or fake gain knob");
}

void test_grid_routes_and_artifacts_fail_closed() {
    auto missing_cell = valid_manifest();
    missing_cell.phase_texture.cells.pop_back();
    expect(has_issue(contract::validate(missing_cell),
                     contract::ContractIssueCode::inconsistent_shape,
                     "phase_texture.cells"),
           "incomplete phase grid was accepted");

    auto missing_route = valid_manifest();
    missing_route.phase_texture.cells[0].routes.pop_back();
    expect(has_issue(contract::validate(missing_route),
                     contract::ContractIssueCode::inconsistent_shape,
                     "phase_texture.cells[0].routes"),
           "incomplete N-route phase cell was accepted");

    auto wrong_residual_size = valid_manifest();
    const auto residual_id =
        wrong_residual_size.phase_texture.cells[0].routes[0]
            .residual_artifact_id;
    const auto artifact = std::ranges::find_if(
        wrong_residual_size.artifacts,
        [&](const auto &value) { return value.id == residual_id; });
    artifact->element_count = 15U;
    artifact->byte_count = 60U;
    expect(has_issue(contract::validate(wrong_residual_size),
                     contract::ContractIssueCode::inconsistent_shape,
                     "phase_texture.cells[0].routes[0].residual_artifact_id"),
           "wrong residual-bank shape was accepted");

    auto wrong_encoding = valid_manifest();
    wrong_encoding.artifacts.front().encoding =
        contract::AudioAtlasArtifactEncoding::complex_float64le;
    wrong_encoding.artifacts.front().byte_count =
        wrong_encoding.artifacts.front().element_count * 16U;
    expect(has_issue(contract::validate(wrong_encoding),
                     contract::ContractIssueCode::inconsistent_shape,
                     "phase_texture.cells[0].routes[0].mean_artifact_id"),
           "wrong phase artifact encoding was accepted");
}

void test_phase_transient_and_presentation_invariants_fail_closed() {
    auto wrong_reference = valid_manifest();
    wrong_reference.phase_texture.cells[0].shift_to_canonical_samples = 1.0;
    expect(has_issue(contract::validate(wrong_reference),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "phase_texture.reference_cell_id"),
           "nonzero canonical phase reference was accepted");

    auto non_power_of_two = valid_manifest();
    non_power_of_two.phase_texture.samples_per_cycle = 10U;
    expect(has_issue(contract::validate(non_power_of_two),
                     contract::ContractIssueCode::invalid_value,
                     "phase_texture.samples_per_cycle"),
           "non-power-of-two canonical phase grid was accepted");

    auto nonzero_boundary = valid_manifest();
    nonzero_boundary.phase_texture.residual_taper.boundary_value = 0.01;
    expect(has_issue(contract::validate(nonzero_boundary),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "phase_texture.residual_taper.boundary_value"),
           "nonzero residual boundary contract was accepted");

    auto rotated_transient = valid_manifest();
    rotated_transient.transient_layers[0]
        .cells[0]
        .source_cycle_origin_ordinal_mod_cycle_count = 3U;
    expect(has_issue(
               contract::validate(rotated_transient),
               contract::ContractIssueCode::invalid_value,
               "transient_layers[0].cells[0].source_cycle_origin_ordinal_mod_cycle_count"),
           "out-of-range transient source-cycle origin was accepted");

    auto missing_direction = valid_manifest();
    missing_direction.transient_layers[1].direction =
        contract::AudioAtlasTransientDirection::rising;
    expect(has_issue(contract::validate(missing_direction),
                     contract::ContractIssueCode::duplicate_identity,
                     "transient_layers[1].direction"),
           "duplicate transient direction was accepted");

    auto wrong_build = valid_manifest();
    wrong_build.presentation.build_identity.sha256 = digest(250U);
    expect(has_issue(contract::validate(wrong_build),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "presentation.build_identity"),
           "presentation/provenance build mismatch was accepted");

    auto invalid_wet_mix = valid_manifest();
    invalid_wet_mix.presentation.source_routes[0].wet_mix_01 = 1.1;
    expect(has_issue(contract::validate(invalid_wet_mix),
                     contract::ContractIssueCode::invalid_value,
                     "presentation.source_routes[0].wet_mix_01"),
           "out-of-range dry/wet mix was accepted");
}

} // namespace

int main() {
    try {
        test_responsive_manifest_is_admitted_and_encoded();
        test_grid_routes_and_artifacts_fail_closed();
        test_phase_transient_and_presentation_invariants_fail_closed();
        std::cout << "audio-atlas contract tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "audio-atlas contract test failure: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
