#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/contract/audio_atlas.hpp"

#include <algorithm>
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

static_assert(noexcept(authoring::parse_atlas_bake_document(std::string_view{})));

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

[[nodiscard]] contract::AudioAtlasManifest valid_manifest() {
    contract::AudioAtlasManifest manifest;
    manifest.id = "bmw-moving-atlas";
    manifest.engine = "bmw-m52tub28-cleanroom";
    manifest.public_seed = 42U;
    manifest.audio.sample_rate_hz = 48000U;
    manifest.audio.buses = {{"master.engine.audition"}};
    manifest.domain = {700.0, 6500.0, -1.0, 1.0};
    manifest.artifacts = {
        {"moving.power.master", "audio/moving-power-master.f32le", 48000U,
         192000U, digest(10U)},
    };

    contract::AudioAtlasMovingSegment segment;
    segment.id = "power-rise-1000-3000";
    segment.direction = contract::AudioAtlasMovingDirection::rising;
    segment.load_coordinate = 1.0;
    segment.state_mask = 3U;
    segment.rpm_slope = {1900.0, 2100.0};
    segment.captured_frames = {0U, 48000U};
    segment.usable_frames = {4800U, 43200U};
    segment.captured_rpm = {1000.0, 3000.0};
    segment.usable_rpm = {1200.0, 2800.0};
    segment.source_scenario = identity("bmw-power-rise-source", 1U);
    segment.capture_configuration = identity("atlas-capture-48k", 2U);
    segment.artifacts = {
        {"master.engine.audition", "moving.power.master"},
    };
    segment.timeline.knots = {
        {0U, 1000.0, 2000.0, 1.0, 1.0, 90000.0, 0.0, 3U, 0U},
        {24000U, 2000.0, 2000.0, 1.0, 1.0, 90000.0, 25.0, 3U, 0U},
        {48000U, 3000.0, 2000.0, 1.0, 1.0, 90000.0, 50.0, 3U, 0U},
    };
    segment.crank_boundaries = {
        {12U, {12000U, 12001U, 0.25}},
        {13U, {14400U, 14401U, 0.75}},
    };
    segment.handoff = {1024U, 25.0, 100.0, 0.1, 0.05};
    manifest.moving_segments = {std::move(segment)};
    manifest.provenance = {
        identity("bmw-m52tub28-cleanroom", 3U),
        identity("bmw-moving-atlas-bake", 4U),
        identity("engine-sim-offline-renderer", 5U),
        {"bmw-atlas-source-inputs", digest(6U)},
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

[[nodiscard]] std::string valid_bake_document() {
    return R"json({
  "schema": "engine-sim-offline/atlas-bake",
  "id": "bmw-moving-atlas",
  "engine": "bmw-m52tub28-cleanroom",
  "public_seed": "42",
  "audio": {
    "sample_rate": {"numerator": "48000", "denominator": "1", "unit": "Hz"},
    "buses": ["master.engine.audition"]
  },
  "domain": {
    "minimum_rpm": 700,
    "maximum_rpm": 6500,
    "minimum_load_coordinate": -1,
    "maximum_load_coordinate": 1
  },
  "scenario_sources": [
    {"id": "power-rise-source", "uri": "scenarios/power-rise.json"}
  ],
  "moving_segments": [
    {
      "id": "power-rise-1000-3000",
      "direction": "rising",
      "load_coordinate": 1,
      "state_mask": 3,
      "rpm_slope": {
        "minimum_rpm_per_second": 1900,
        "maximum_rpm_per_second": 2100
      },
      "captured_rpm": {"minimum": 1000, "maximum": 3000},
      "usable_rpm": {"minimum": 1200, "maximum": 2800},
      "scenario": "power-rise-source",
      "handoff": {
        "transition_frames": 1024,
        "maximum_rpm_error": 25,
        "maximum_rpm_slope_error_rpm_per_second": 100,
        "maximum_load_error": 0.1,
        "maximum_crank_phase_error_revolutions": 0.05
      }
    }
  ],
  "stationary_tiles": [],
  "transient_performances": [],
  "lifecycle_performances": []
})json";
}

void replace_once(std::string &text, std::string_view before,
                  std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error{"test mutation source text was not found"};
    }
    text.replace(position, before.size(), after);
}

[[nodiscard]] const authoring::DiagnosticReport &
require_report(const authoring::AtlasBakeDocumentParseResult &result) {
    const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{"invalid atlas-bake document was accepted"};
    }
    return *report;
}

[[nodiscard]] bool has_diagnostic(const authoring::DiagnosticReport &report,
                                  authoring::DiagnosticCode code,
                                  std::string_view pointer) {
    return std::ranges::any_of(report.diagnostics, [&](const auto &diagnostic) {
        return diagnostic.code == code && diagnostic.json_pointer == pointer;
    });
}

void test_minimal_moving_atlas_is_admitted() {
    expect(contract::validate(valid_manifest()).ok(),
           "valid moving audio atlas was rejected");

    const auto parsed = authoring::parse_atlas_bake_document(valid_bake_document());
    const auto *document = std::get_if<authoring::AtlasBakeDocument>(&parsed);
    expect(document != nullptr && document->moving_segments.size() == 1U &&
               document->stationary_tiles.empty() &&
               document->transient_performances.empty() &&
               document->lifecycle_performances.empty(),
           "valid first-slice atlas-bake document was not retained");
}

void test_manifest_timeline_ranges_and_references_fail_closed() {
    auto future_representation = valid_manifest();
    future_representation.stationary_tiles.emplace_back();
    expect(has_issue(contract::validate(future_representation),
                     contract::ContractIssueCode::unsupported_value,
                     "stationary_tiles"),
           "unadmitted stationary atlas material was accepted");

    auto nonchronological = valid_manifest();
    nonchronological.moving_segments[0].timeline.knots[1].frame = 48000U;
    expect(!contract::validate(nonchronological).ok(),
           "nonchronological atlas timeline was accepted");

    auto escaped_usable_range = valid_manifest();
    escaped_usable_range.moving_segments[0].usable_frames.end = 48000U;
    expect(!contract::validate(escaped_usable_range).ok(),
           "non-interior atlas usable range was accepted");

    auto missing_artifact = valid_manifest();
    missing_artifact.moving_segments[0].artifacts[0].artifact_id = "missing";
    expect(has_issue(contract::validate(missing_artifact),
                     contract::ContractIssueCode::dangling_reference,
                     "moving_segments[0].artifacts[0].artifact_id"),
           "dangling atlas artifact reference was accepted");

    auto missing_bus = valid_manifest();
    missing_bus.moving_segments[0].artifacts[0].bus_id = "exhaust.missing";
    expect(has_issue(contract::validate(missing_bus),
                     contract::ContractIssueCode::dangling_reference,
                     "moving_segments[0].artifacts[0].bus_id"),
           "dangling atlas bus reference was accepted");
}

void test_bake_grammar_and_future_arrays_are_strict() {
    auto wrong_schema = valid_bake_document();
    replace_once(wrong_schema, "engine-sim-offline/atlas-bake",
                 "engine-sim-offline/atlas-bake-v1");
    expect(has_diagnostic(
               require_report(authoring::parse_atlas_bake_document(wrong_schema)),
               authoring::DiagnosticCode::unsupported_schema, "/schema"),
           "versioned atlas-bake schema alias was accepted");

    auto unknown = valid_bake_document();
    replace_once(unknown, R"json("lifecycle_performances": [])json",
                 R"json("lifecycle_performances": [], "compatibility": 1)json");
    expect(has_diagnostic(require_report(authoring::parse_atlas_bake_document(unknown)),
                          authoring::DiagnosticCode::unknown_field,
                          "/compatibility"),
           "unknown atlas-bake member was accepted");

    auto future_representation = valid_bake_document();
    replace_once(future_representation, R"json("stationary_tiles": [])json",
                 R"json("stationary_tiles": [{}])json");
    expect(has_diagnostic(
               require_report(
                   authoring::parse_atlas_bake_document(future_representation)),
               authoring::DiagnosticCode::unsupported_capability,
               "/stationary_tiles"),
           "unadmitted authored stationary material was accepted");
}

} // namespace

int main() {
    try {
        test_minimal_moving_atlas_is_admitted();
        test_manifest_timeline_ranges_and_references_fail_closed();
        test_bake_grammar_and_future_arrays_are_strict();
        std::cout << "audio-atlas contract tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "audio-atlas contract test failure: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
