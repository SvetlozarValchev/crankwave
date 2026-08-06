#include "engine_sim_offline/authoring/parse.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace engine_sim_offline::authoring;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"failed to open canonical atlas-bake document"};
    }
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void replace_once(std::string &text, std::string_view before,
                  std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos) {
        throw std::runtime_error{"atlas-bake mutation source was not found"};
    }
    text.replace(position, before.size(), after);
}

[[nodiscard]] const DiagnosticReport &
require_report(const AtlasBakeDocumentParseResult &result) {
    const auto *report = std::get_if<DiagnosticReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{"invalid atlas-bake document was accepted"};
    }
    return *report;
}

[[nodiscard]] bool has_diagnostic(const DiagnosticReport &report,
                                  DiagnosticCode code,
                                  std::string_view pointer) {
    return std::ranges::any_of(report.diagnostics, [&](const auto &diagnostic) {
        return diagnostic.code == code && diagnostic.json_pointer == pointer;
    });
}

void expect_diagnostic(const std::string &json, DiagnosticCode code,
                       std::string_view pointer, std::string_view message) {
    const auto parsed = parse_atlas_bake_document(json);
    expect(has_diagnostic(require_report(parsed), code, pointer), message);
}

void test_canonical_document(const std::string &canonical) {
    const auto parsed = parse_atlas_bake_document(canonical);
    const auto *document = std::get_if<AtlasBakeDocument>(&parsed);
    expect(document != nullptr, "canonical atlas-bake document was rejected");
    expect(document->schema == kAtlasBakeSchema &&
               document->rpm_anchors.size() == 10U &&
               document->load_lanes.size() == 3U &&
               document->audio.routes.size() == 2U &&
               document->capture.samples_per_cycle == 4096U &&
               document->transient_capture.motion ==
                   AtlasBakeTransientMotion::prescribed_exponential_speed,
           "canonical atlas-bake document was not retained exactly");
}

void test_unknown_member_fails_closed(const std::string &canonical) {
    auto unknown = canonical;
    replace_once(unknown, R"json("lifecycle_captures": [])json",
                 R"json("lifecycle_captures": [],
  "compatibility": true)json");
    expect_diagnostic(unknown, DiagnosticCode::unknown_field, "/compatibility",
                      "unknown atlas-bake member was accepted");
}

void test_missing_required_member_fails_closed(const std::string &canonical) {
    auto missing = canonical;
    replace_once(
        missing,
        R"json(    "routes": ["exhaust.front", "exhaust.rear"],
    "output_bus": "master-engine-audition")json",
        R"json(    "routes": ["exhaust.front", "exhaust.rear"])json");
    expect_diagnostic(missing, DiagnosticCode::missing_value,
                      "/audio/output_bus",
                      "missing atlas output bus was accepted");
}

void test_reference_fails_closed(const std::string &canonical) {
    auto dangling = canonical;
    replace_once(dangling, R"json("load_lane": "power")json",
                 R"json("load_lane": "missing")json");
    expect_diagnostic(dangling, DiagnosticCode::dangling_reference,
                      "/phase_alignment/reference/load_lane",
                      "dangling phase-alignment lane was accepted");
}

void test_anchor_order_fails_closed(const std::string &canonical) {
    auto unordered = canonical;
    replace_once(unordered, "    5000,\n    6000,",
                 "    6000,\n    5000,");
    expect_diagnostic(unordered, DiagnosticCode::inconsistent_value,
                      "/rpm_anchors/8",
                      "unordered RPM anchors were accepted");
}

void test_phase_resolution_fails_closed(const std::string &canonical) {
    auto non_power_of_two = canonical;
    replace_once(non_power_of_two, R"json("samples_per_cycle": 4096)json",
                 R"json("samples_per_cycle": 4095)json");
    expect_diagnostic(non_power_of_two, DiagnosticCode::out_of_range,
                      "/capture/samples_per_cycle",
                      "non-power-of-two phase resolution was accepted");
}

void test_transient_motion_fails_closed(const std::string &canonical) {
    auto unknown_motion = canonical;
    replace_once(unknown_motion,
                 R"json("motion": "prescribed-exponential-speed")json",
                 R"json("motion": "held-dyno")json");
    expect_diagnostic(unknown_motion, DiagnosticCode::invalid_value,
                      "/transient_capture/motion",
                      "unsupported transient motion mode was accepted");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository source directory"};
        }
        const auto canonical = read_text(
            std::filesystem::path{argv[1]} /
            "data/engines/bmw-m52tub28-cleanroom/atlas-bake.json");
        test_canonical_document(canonical);
        test_unknown_member_fails_closed(canonical);
        test_missing_required_member_fails_closed(canonical);
        test_reference_fails_closed(canonical);
        test_anchor_order_fails_closed(canonical);
        test_phase_resolution_fails_closed(canonical);
        test_transient_motion_fails_closed(canonical);
        std::cout << "atlas-bake authoring tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "atlas-bake authoring test failure: " << exception.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
