#include "engine_sim_offline/authoring/parse.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline::authoring;

static_assert(noexcept(parse_package_bake_document(std::string_view{})));

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string valid_document() {
    return R"json({
  "schema": "engine-sim-offline/package-bake",
  "id": "bmw-m52tub28-responsive",
  "engine": "bmw-m52tub28-cleanroom",
  "public_seed": "18446744073709551615",
  "audio": {
    "sample_rate": {
      "numerator": "48000",
      "denominator": "1",
      "unit": "Hz"
    },
    "buses": ["master.engine.audition", "exhaust.front"]
  },
  "scenario_sources": [
    {"id": "coast-source", "uri": "scenarios/package-coast.json"},
    {"id": "part-source", "uri": "scenarios/package-part.json"},
    {"id": "power-source", "uri": "scenarios/package-power.json"},
    {"id": "idle-source", "uri": "scenarios/package-idle.json"}
  ],
  "running": {
    "rpm_range": {
      "minimum": {"value": 650, "unit": "rpm"},
      "maximum": {"value": 6500, "unit": "rpm"}
    },
    "planes": [
      {
        "id": "coast",
        "load_coordinate": -1,
        "direction": "falling",
        "scenario": "coast-source"
      },
      {
        "id": "part-load",
        "load_coordinate": 0,
        "direction": "rising",
        "scenario": "part-source"
      },
      {
        "id": "power",
        "load_coordinate": 1,
        "direction": "rising",
        "scenario": "power-source"
      }
    ],
    "idle": {"scenario": "idle-source"}
  },
  "events": []
})json";
}

void replace_once(std::string &text, std::string_view before, std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error{"test mutation source text was not found"};
    }
    text.replace(position, before.size(), after);
}

[[nodiscard]] PackageBakeDocument require_document(std::string_view json) {
    auto result = parse_package_bake_document(json);
    if (const auto *report = std::get_if<DiagnosticReport>(&result)) {
        const auto message = report->diagnostics.empty()
                                 ? std::string{"unknown parser diagnostic"}
                                 : report->diagnostics.front().message;
        throw std::runtime_error{"valid package-bake document was rejected: " +
                                 message};
    }
    return std::get<PackageBakeDocument>(std::move(result));
}

[[nodiscard]] const DiagnosticReport &
require_report(const PackageBakeDocumentParseResult &result) {
    const auto *report = std::get_if<DiagnosticReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{"invalid package-bake document was accepted"};
    }
    return *report;
}

[[nodiscard]] bool has_diagnostic(const DiagnosticReport &report, DiagnosticCode code,
                                  std::string_view json_pointer) {
    for (const auto &diagnostic : report.diagnostics) {
        if (diagnostic.code == code && diagnostic.json_pointer == json_pointer) {
            return true;
        }
    }
    return false;
}

void test_complete_contract_is_retained() {
    const auto document = require_document(valid_document());
    expect(document.schema == "engine-sim-offline/package-bake" &&
               document.id.value == "bmw-m52tub28-responsive" &&
               document.engine.value == "bmw-m52tub28-cleanroom",
           "package or engine identity changed during parsing");
    expect(document.public_seed == 18446744073709551615ULL,
           "exact package selector seed changed during parsing");
    expect(document.audio.sample_rate.numerator == 48000U &&
               document.audio.sample_rate.denominator == 1U &&
               document.audio.sample_rate.unit == "Hz" &&
               document.audio.buses.size() == 2U &&
               document.audio.buses[1].value == "exhaust.front",
           "ordered package audio contract changed during parsing");
    expect(document.scenario_sources.size() == 4U &&
               document.scenario_sources.front().id.value == "coast-source" &&
               document.scenario_sources.back().uri == "scenarios/package-idle.json",
           "scenario-source registry changed during parsing");
    expect(document.running.rpm_range.minimum.value == 650.0 &&
               document.running.rpm_range.maximum.value == 6500.0 &&
               document.running.planes.size() == 3U &&
               document.running.planes[0].load_coordinate == -1.0 &&
               document.running.planes[0].direction ==
                   PackageBakeRunningDirection::falling &&
               document.running.planes[2].scenario.value == "power-source" &&
               document.running.idle.scenario.value == "idle-source" &&
               document.events.empty(),
           "running package-bake contract changed during parsing");
}

void test_schema_members_and_empty_audio_are_strict() {
    std::string wrong_schema = valid_document();
    replace_once(wrong_schema, "engine-sim-offline/package-bake",
                 "engine-sim-offline/package-bake-v1");
    expect(has_diagnostic(require_report(parse_package_bake_document(wrong_schema)),
                          DiagnosticCode::unsupported_schema, "/schema"),
           "versioned package-bake schema alias was accepted");

    std::string unknown = valid_document();
    replace_once(unknown, R"json("events": [])json",
                 R"json("events": [], "compatibility_version": 1)json");
    expect(has_diagnostic(require_report(parse_package_bake_document(unknown)),
                          DiagnosticCode::unknown_field, "/compatibility_version"),
           "unknown package-bake root member was accepted");

    std::string empty_buses = valid_document();
    replace_once(empty_buses,
                 R"json("buses": ["master.engine.audition", "exhaust.front"])json",
                 R"json("buses": [])json");
    expect(has_diagnostic(require_report(parse_package_bake_document(empty_buses)),
                          DiagnosticCode::out_of_range, "/audio/buses"),
           "empty package audio bus list was accepted");

    std::string duplicate_buses = valid_document();
    replace_once(duplicate_buses, R"json("exhaust.front")json",
                 R"json("master.engine.audition")json");
    expect(has_diagnostic(require_report(parse_package_bake_document(duplicate_buses)),
                          DiagnosticCode::duplicate_id, "/audio/buses/1"),
           "duplicate package audio bus reference was accepted");
}

void test_running_domain_and_plane_order_are_strict() {
    std::string reversed_range = valid_document();
    replace_once(reversed_range, R"json("maximum": {"value": 6500, "unit": "rpm"})json",
                 R"json("maximum": {"value": 60, "unit": "rad/s"})json");
    expect(has_diagnostic(require_report(parse_package_bake_document(reversed_range)),
                          DiagnosticCode::inconsistent_value,
                          "/running/rpm_range/maximum"),
           "inverted mixed-unit running RPM range was accepted");

    std::string too_few = valid_document();
    const auto part_start = too_few.find(R"json(      {
        "id": "part-load")json");
    const auto power_start = too_few.find(R"json(      {
        "id": "power")json");
    expect(part_start != std::string::npos && power_start != std::string::npos,
           "plane-removal test fixture changed");
    too_few.erase(part_start, power_start - part_start);
    const auto too_few_result = parse_package_bake_document(too_few);
    expect(has_diagnostic(require_report(too_few_result), DiagnosticCode::out_of_range,
                          "/running/planes"),
           "two-plane package was accepted");

    std::string unordered = valid_document();
    replace_once(unordered, R"json("load_coordinate": 0)json",
                 R"json("load_coordinate": -1)json");
    const auto unordered_result = parse_package_bake_document(unordered);
    expect(has_diagnostic(require_report(unordered_result),
                          DiagnosticCode::inconsistent_value,
                          "/running/planes/1/load_coordinate"),
           "non-ascending load planes were accepted");

    std::string missing_endpoint = valid_document();
    replace_once(missing_endpoint, R"json("load_coordinate": 1)json",
                 R"json("load_coordinate": 0.75)json");
    expect(has_diagnostic(require_report(parse_package_bake_document(missing_endpoint)),
                          DiagnosticCode::inconsistent_value,
                          "/running/planes/2/load_coordinate"),
           "running planes without the exact +1 endpoint were accepted");
}

void test_scenario_source_graph_is_closed_and_unique() {
    std::string duplicate_id = valid_document();
    replace_once(duplicate_id, R"json({"id": "part-source")json",
                 R"json({"id": "coast-source")json");
    expect(has_diagnostic(require_report(parse_package_bake_document(duplicate_id)),
                          DiagnosticCode::duplicate_id, "/scenario_sources/1/id"),
           "duplicate scenario-source ID was accepted");

    std::string dangling = valid_document();
    replace_once(dangling, R"json("scenario": "power-source")json",
                 R"json("scenario": "missing-source")json");
    expect(has_diagnostic(require_report(parse_package_bake_document(dangling)),
                          DiagnosticCode::dangling_reference,
                          "/running/planes/2/scenario"),
           "missing running-plane scenario source was accepted");

    std::string duplicate_reference = valid_document();
    replace_once(duplicate_reference, R"json("scenario": "part-source")json",
                 R"json("scenario": "coast-source")json");
    expect(
        has_diagnostic(require_report(parse_package_bake_document(duplicate_reference)),
                       DiagnosticCode::duplicate_id, "/running/planes/1/scenario"),
        "duplicate running scenario-source reference was accepted");

    std::string unused = valid_document();
    replace_once(
        unused,
        R"json({"id": "idle-source", "uri": "scenarios/package-idle.json"})json",
        R"json({"id": "idle-source", "uri": "scenarios/package-idle.json"},
    {"id": "unused-source", "uri": "scenarios/package-unused.json"})json");
    expect(has_diagnostic(require_report(parse_package_bake_document(unused)),
                          DiagnosticCode::disconnected_object,
                          "/scenario_sources/4/id"),
           "unreferenced scenario-source definition was accepted");
}

void test_lifecycle_events_are_not_prematurely_admitted() {
    std::string document = valid_document();
    replace_once(document, R"json("events": [])json",
                 R"json("events": [{"type": "startup"}])json");
    expect(has_diagnostic(require_report(parse_package_bake_document(document)),
                          DiagnosticCode::unsupported_capability, "/events"),
           "nonempty package event array was accepted by the first gate");
}

} // namespace

int main() {
    try {
        test_complete_contract_is_retained();
        test_schema_members_and_empty_audio_are_strict();
        test_running_domain_and_plane_order_are_strict();
        test_scenario_source_graph_is_closed_and_unique();
        test_lifecycle_events_are_not_prematurely_admitted();
        std::cout << "package-bake authoring tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "package-bake authoring test failure: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
