#include "engine_sim_offline/artifacts/audio_package_manifest_encoder.hpp"
#include "engine_sim_offline/contract/audio_package.hpp"

#include <algorithm>
#include <cstddef>
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

[[nodiscard]] contract::AudioPackageContentIdentity identity(std::string id,
                                                             std::uint8_t hash) {
    return {std::move(id), digest(hash)};
}

[[nodiscard]] contract::AudioPackageCycleUnit
unit(std::uint64_t ordinal, std::uint64_t start_frame, std::uint64_t end_frame,
     double canonical_rpm, double measured_rpm, double load, double torque,
     double throttle) {
    return {
        ordinal,
        {start_frame, start_frame + 1U, 0.25},
        {end_frame, end_frame + 1U, 0.75},
        canonical_rpm,
        measured_rpm,
        load,
        torque,
        throttle,
        throttle - 0.01,
        3U,
        0U,
    };
}

[[nodiscard]] contract::AudioPackageRunningPlane
plane(std::string id, double coordinate,
      contract::AudioPackageRunningDirection direction, std::string scenario_id,
      std::uint8_t scenario_hash, std::string master_artifact_id,
      std::string exhaust_artifact_id,
      std::vector<contract::AudioPackageCycleUnit> units) {
    return {
        std::move(id),
        coordinate,
        direction,
        identity(std::move(scenario_id), scenario_hash),
        {
            {"master.engine.audition", std::move(master_artifact_id)},
            {"exhaust.front", std::move(exhaust_artifact_id)},
        },
        std::move(units),
    };
}

[[nodiscard]] contract::AudioPackageManifest valid_manifest() {
    contract::AudioPackageManifest manifest;
    manifest.identity = {
        "bmw-m52tub28-responsive",
        identity("bmw-m52tub28-cleanroom", 1U),
        identity("bmw-normal-running-package-bake", 2U),
    };
    manifest.provenance = {
        identity("engine-sim-offline-build", 3U),
        {"bmw-m52tub28-source-inputs", digest(4U)},
    };
    manifest.audio = {
        {192000U, 1U},
        contract::AudioPackageAudioContainer::wav,
        contract::AudioSampleEncoding::float32le,
        contract::AudioChannelLayout::mono,
    };
    manifest.buses = {
        {"master.engine.audition",
         contract::AudioPackageBusKind::master_engine_audition,
         contract::AudioPackageBusDisposition::monitor_mix, std::nullopt},
        {"exhaust.front", contract::AudioPackageBusKind::source_route,
         contract::AudioPackageBusDisposition::positional_emitter,
         contract::AudioPackageSourceRouteDescriptor{
             contract::SourceRouteKind::exhaust_outlet,
             "exhaust.front",
             "engine.exhaust.front",
         }},
    };
    manifest.running.cycle_revolutions = 2U;
    manifest.running.selector_seed = UINT64_C(18446744073709551615);
    manifest.running.cycle_signal_alignment_frames = 1228.8;
    manifest.running.rpm_grid = {
        1000.0, 1500.0, 2000.0, 2500.0, 500.0, 1U, 1U, 4U, 25.0,
    };
    manifest.running.planes = {
        plane("coast", -1.0, contract::AudioPackageRunningDirection::falling,
              "bmw-package-coast-source", 10U, "plane.coast.master",
              "plane.coast.exhaust",
              {
                  unit(33U, 130U, 140U, 1000.0, 1005.0, -1.0, -34.0, 0.04),
                  unit(32U, 100U, 110U, 1500.0, 1494.0, -1.0, -38.0, 0.04),
                  unit(31U, 70U, 80U, 2000.0, 2004.0, -1.0, -42.0, 0.04),
                  unit(30U, 40U, 50U, 2500.0, 2496.0, -1.0, -46.0, 0.04),
              }),
        plane("part-load", 0.0, contract::AudioPackageRunningDirection::rising,
              "bmw-package-part-source", 11U, "plane.part.master", "plane.part.exhaust",
              {
                  unit(40U, 10U, 20U, 1000.0, 1003.0, 0.0, 9.0, 0.20),
                  unit(41U, 40U, 50U, 1500.0, 1496.0, 0.0, 12.0, 0.20),
                  unit(42U, 70U, 80U, 2000.0, 2004.0, 0.0, 15.0, 0.20),
                  unit(43U, 100U, 110U, 2500.0, 2498.0, 0.0, 18.0, 0.20),
              }),
        plane("power", 1.0, contract::AudioPackageRunningDirection::rising,
              "bmw-package-power-source", 12U, "plane.power.master",
              "plane.power.exhaust",
              {
                  unit(50U, 10U, 20U, 1000.0, 1002.0, 1.0, 145.0, 1.00),
                  unit(51U, 40U, 50U, 1500.0, 1498.0, 1.0, 165.0, 1.00),
                  unit(52U, 70U, 80U, 2000.0, 2003.0, 1.0, 190.0, 1.00),
                  unit(53U, 100U, 110U, 2500.0, 2497.0, 1.0, 205.0, 1.00),
              }),
    };
    manifest.running.idle = {
        identity("bmw-package-idle-source", 13U),
        {
            {"master.engine.audition", "idle.master"},
            {"exhaust.front", "idle.exhaust"},
        },
        {
            unit(60U, 10U, 20U, 700.0, 702.0, -0.02, -2.0, 0.08),
            unit(61U, 30U, 40U, 705.0, 704.0, 0.01, 1.0, 0.08),
        },
    };
    manifest.running.idle.units[0].start = {10U, 10U, 0.0};
    manifest.artifacts = {
        {"idle.exhaust", "audio/idle/exhaust-front.wav", 100U, 444U, digest(24U)},
        {"idle.master", "audio/idle/master.wav", 100U, 444U, digest(20U)},
        {"plane.coast.exhaust", "audio/coast/exhaust-front.wav", 200U, 844U,
         digest(25U)},
        {"plane.coast.master", "audio/coast/master.wav", 200U, 844U, digest(21U)},
        {"plane.part.exhaust", "audio/part/exhaust-front.wav", 200U, 844U, digest(26U)},
        {"plane.part.master", "audio/part/master.wav", 200U, 844U, digest(22U)},
        {"plane.power.exhaust", "audio/power/exhaust-front.wav", 200U, 844U,
         digest(27U)},
        {"plane.power.master", "audio/power/master.wav", 200U, 844U, digest(23U)},
    };
    return manifest;
}

[[nodiscard]] bool has_issue(const contract::ValidationReport &report,
                             contract::ContractIssueCode code, std::string_view path) {
    return std::ranges::any_of(report.issues, [&](const auto &issue) {
        return issue.code == code && issue.path == path;
    });
}

[[nodiscard]] const artifacts::ManifestEncoding &
require_encoding(const artifacts::ManifestEncodingResult &result) {
    const auto *encoding = std::get_if<artifacts::ManifestEncoding>(&result);
    if (encoding == nullptr) {
        const auto &error = std::get<RenderSinkError>(result);
        throw std::runtime_error{"valid package encoding failed: " + error.message};
    }
    return *encoding;
}

[[nodiscard]] std::string as_string(const artifacts::ManifestEncoding &encoding) {
    std::string result;
    result.reserve(encoding.bytes.size());
    for (const auto byte : encoding.bytes) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

void test_valid_contract_and_deterministic_runtime_json() {
    const auto manifest = valid_manifest();
    expect(contract::validate(manifest).ok(), "valid audio package was rejected");

    const auto first = artifacts::encode_audio_package_manifest(manifest);
    const auto second = artifacts::encode_audio_package_manifest(manifest);
    expect(require_encoding(first).bytes == require_encoding(second).bytes,
           "audio package encoding is not deterministic");

    const auto document = as_string(require_encoding(first));
    expect(document.starts_with(
               "{\"schema\":\"engine-sim-offline/audio-package\",\"identity\":"),
           "audio package root or canonical field order changed");
    expect(document.find("\"sample_rate\":{\"numerator\":192000,"
                         "\"denominator\":1}") != std::string::npos,
           "exact rational rate is not runtime-readable JSON");
    expect(document.find("\"container\":\"wav\",\"encoding\":\"float32le\"") !=
               std::string::npos,
           "package payload container or encoding is ambiguous");
    expect(document.find("\"selector_seed\":"
                         "\"18446744073709551615\"") != std::string::npos,
           "full uint64 selector seed lost exact decimal identity");
    expect(document.find("\"cycle_signal_alignment_frames\":1228.8") !=
               std::string::npos,
           "cycle signal-alignment offset was not encoded canonically");
    expect(document.find("\"canonical_rpm\":1000") != std::string::npos &&
               document.find("0x") == std::string::npos,
           "binary64 package values regressed to internal bit strings");
    expect(document.find("\"source_route\":{\"kind\":\"exhaust_outlet\","
                         "\"semantic_id\":\"exhaust.front\","
                         "\"emitter_anchor_id\":\"engine.exhaust.front\"}") !=
               std::string::npos,
           "positional route-stem metadata was not retained");
    expect(document.ends_with("}\n") &&
               std::count(document.begin(), document.end(), '\n') == 1,
           "package JSON contains non-terminal whitespace");
}

void test_plane_and_artifact_order_are_closed() {
    auto duplicate_bus = valid_manifest();
    duplicate_bus.buses.push_back(duplicate_bus.buses.front());
    expect(has_issue(contract::validate(duplicate_bus),
                     contract::ContractIssueCode::duplicate_identity, "buses[2].id"),
           "duplicate package bus was accepted");

    auto duplicate_plane = valid_manifest();
    duplicate_plane.running.planes[1].id = duplicate_plane.running.planes[0].id;
    expect(has_issue(contract::validate(duplicate_plane),
                     contract::ContractIssueCode::duplicate_identity,
                     "running.planes[1].id"),
           "duplicate running plane was accepted");

    auto unordered_planes = valid_manifest();
    std::swap(unordered_planes.running.planes[0], unordered_planes.running.planes[1]);
    expect(has_issue(contract::validate(unordered_planes),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "running.planes[1].load_coordinate"),
           "unordered load planes were accepted");

    auto mismatched_unit_load = valid_manifest();
    mismatched_unit_load.running.planes[1].units[0].average_signed_load = 0.1;
    expect(has_issue(contract::validate(mismatched_unit_load),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "running.planes[1].units[0].average_signed_load"),
           "cycle load coordinate outside its authored plane was accepted");

    auto unordered_calibration = valid_manifest();
    unordered_calibration.running.planes[1]
        .units[0]
        .average_net_torque_nm = -40.0;
    expect(has_issue(contract::validate(unordered_calibration),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "running.planes[1].units[0].average_net_torque_nm"),
           "non-increasing per-row load calibration was accepted");

    auto unordered_artifacts = valid_manifest();
    std::swap(unordered_artifacts.artifacts[0], unordered_artifacts.artifacts[1]);
    expect(has_issue(contract::validate(unordered_artifacts),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "artifacts[1].id"),
           "noncanonical artifact order was accepted");

    auto duplicate_path = valid_manifest();
    duplicate_path.artifacts[1].relative_path =
        duplicate_path.artifacts[0].relative_path;
    expect(has_issue(contract::validate(duplicate_path),
                     contract::ContractIssueCode::duplicate_identity,
                     "artifacts[1].relative_path"),
           "duplicate portable artifact path was accepted");
}

void test_boundaries_references_and_units_fail_closed() {
    auto false_padding = valid_manifest();
    false_padding.running.rpm_grid.minimum_rpm = 1100.0;
    expect(has_issue(contract::validate(false_padding),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "running.rpm_grid.minimum_rpm"),
           "grid padding metadata without matching rows was accepted");

    auto missing_edge_context = valid_manifest();
    missing_edge_context.running.planes[1].units[0].start = {3U, 4U, 0.5};
    expect(has_issue(contract::validate(missing_edge_context),
                     contract::ContractIssueCode::inconsistent_shape,
                     "running.planes[1].units[0].start.left_frame"),
           "cycle boundary without its declared source context was accepted");

    auto malformed_boundary = valid_manifest();
    auto &boundary = malformed_boundary.running.planes[1].units[0].start;
    boundary.right_frame = boundary.left_frame;
    boundary.fraction_from_left_01 = 0.5;
    expect(has_issue(contract::validate(malformed_boundary),
                     contract::ContractIssueCode::invalid_value,
                     "running.planes[1].units[0].start"),
           "fractional equal-bracket boundary was accepted");

    auto dangling = valid_manifest();
    dangling.running.planes[2].artifacts[0].artifact_id = "missing.master";
    expect(has_issue(contract::validate(dangling),
                     contract::ContractIssueCode::dangling_reference,
                     "running.planes[2].artifacts[0].artifact_id"),
           "dangling lane artifact was accepted");

    auto duplicate_unit = valid_manifest();
    duplicate_unit.running.planes[1].units[1].completed_cycle_ordinal =
        duplicate_unit.running.planes[1].units[0].completed_cycle_ordinal;
    expect(has_issue(contract::validate(duplicate_unit),
                     contract::ContractIssueCode::duplicate_identity,
                     "running.planes[1].units[1].completed_cycle_ordinal"),
           "duplicate cycle unit was accepted");

    auto transitioning_unit = valid_manifest();
    transitioning_unit.running.idle.units[0].transition_mask = 1U;
    expect(has_issue(contract::validate(transitioning_unit),
                     contract::ContractIssueCode::inconsistent_semantics,
                     "running.idle.units[0].transition_mask"),
           "normal-running unit with a discrete transition was accepted");

    const auto failed_encoding =
        artifacts::encode_audio_package_manifest(malformed_boundary);
    expect(std::holds_alternative<RenderSinkError>(failed_encoding) &&
               std::get<RenderSinkError>(failed_encoding).kind ==
                   RenderSinkErrorKind::protocol_violation,
           "invalid package reached deterministic JSON publication");
}

} // namespace

int main() {
    try {
        test_valid_contract_and_deterministic_runtime_json();
        test_plane_and_artifact_order_are_closed();
        test_boundaries_references_and_units_fail_closed();
        std::cout << "audio package contract tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "audio package contract test failure: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
