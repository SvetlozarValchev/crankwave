#include "reference/bmw_p18_render_specification.hpp"

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_inertial_dyno_listening_request.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_fixture_loader.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

[[nodiscard]] std::string
validation_report_text(const contract::ValidationReport &report) {
    std::ostringstream text;
    for (const auto &issue : report.issues) {
        text << "\n  " << issue.path << ": " << issue.message;
    }
    return text.str();
}

[[nodiscard]] const reference::P18ExpectedLineageFile &configured_ir_catalog_record() {
    const auto &catalog = reference::p18_reference_catalog_v1();
    const auto found =
        std::ranges::find(catalog.expected_lineage_files,
                          reference::P18ReferenceLineageFile::configured_ir_input,
                          &reference::P18ExpectedLineageFile::file);
    if (found == catalog.expected_lineage_files.end()) {
        throw std::logic_error{"P1.8 catalog is missing its configured IR"};
    }
    return *found;
}

[[nodiscard]] std::vector<std::byte>
read_verified_configured_ir(const std::filesystem::path &fixture_root) {
    const auto &expected = configured_ir_catalog_record();

    // Verify the whole accepted local-evaluation capsule once. The decoded audit,
    // seeds, and kernel are destroyed before render construction and never become
    // simulation or presentation inputs.
    {
        const auto fixture = reference::load_p18_reference_fixture(fixture_root);
        const auto &observed = fixture.verified_lineage.at(
            reference::P18ReferenceLineageFile::configured_ir_input);
        if (observed.byte_count != expected.expected_byte_count ||
            observed.payload_sha256 != expected.expected_sha256) {
            throw std::runtime_error{
                "verified P1.8 IR identity differs from the immutable catalog"};
        }
    }

    if (expected.expected_byte_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        expected.expected_byte_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error{"P1.8 configured IR exceeds host I/O bounds"};
    }
    const auto path = fixture_root / expected.expected_relative_path;
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input.is_open()) {
        throw std::runtime_error{"could not reopen the verified P1.8 configured IR"};
    }
    const auto measured = input.tellg();
    if (measured < 0 ||
        static_cast<std::uint64_t>(measured) != expected.expected_byte_count) {
        throw std::runtime_error{
            "reopened P1.8 configured IR has a different byte count"};
    }
    input.seekg(0);

    std::vector<std::byte> bytes(
        static_cast<std::size_t>(expected.expected_byte_count));
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        contract::sha256(bytes) != expected.expected_sha256) {
        throw std::runtime_error{
            "reopened P1.8 configured IR differs from its verified identity"};
    }
    return bytes;
}

[[nodiscard]] profiles::BmwM52b28InertialDynoListeningRequest make_listening_request() {
    auto result = profiles::make_bmw_m52b28_inertial_dyno_listening_request();
    if (auto *request =
            std::get_if<profiles::BmwM52b28InertialDynoListeningRequest>(&result)) {
        return std::move(*request);
    }
    throw std::runtime_error{
        "canonical M4 BMW listening request construction failed" +
        validation_report_text(std::get<contract::ValidationReport>(result))};
}

[[noreturn]] void throw_render_outcome(const contract::RenderResult &result) {
    if (const auto *failure = std::get_if<contract::RenderFailure>(&result)) {
        throw std::runtime_error{"M4 BMW render failed (" +
                                 failure->context.detail_code +
                                 "): " + failure->context.state_summary +
                                 validation_report_text(failure->validation)};
    }
    const auto &unreachable = std::get<contract::UnreachableTarget>(result);
    throw std::runtime_error{
        "M4 BMW inertial-dyno request unexpectedly returned unreachable-target (" +
        unreachable.context.detail_code + "): " + unreachable.context.state_summary};
}

int run(int argc, char **argv) {
    if (argc != 3) {
        throw std::invalid_argument{
            "usage: engine-sim-offline-m4-bmw-listening <fixture-root> "
            "<new-output-directory>"};
    }
    const std::filesystem::path fixture_root{argv[1]};
    const std::filesystem::path output_directory{argv[2]};
    const auto publication_name = output_directory.filename().string();
    auto publication_root = output_directory.parent_path();
    if (output_directory.empty() || publication_name.empty()) {
        throw std::invalid_argument{
            "new output directory must end in one publication-name component"};
    }
    if (publication_root.empty()) {
        publication_root = ".";
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(publication_root, filesystem_error) ||
        filesystem_error) {
        throw std::invalid_argument{
            "output parent must be an existing accessible directory"};
    }

    const auto command_started = std::chrono::steady_clock::now();
    auto request = make_listening_request();
    auto configured_ir = read_verified_configured_ir(fixture_root);
    auto specification = reference::make_bmw_p18_render_specification(
        std::move(request.engine), std::move(request.provenance),
        std::move(configured_ir));

    artifacts::DirectoryRenderSink sink{publication_root, publication_name};
    const auto render_started = std::chrono::steady_clock::now();
    const auto result = render(specification, request.scenario, sink);
    const double render_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - render_started)
            .count();

    const auto result_validation = validate(result, specification, request.scenario);
    if (!result_validation.ok()) {
        throw std::runtime_error{"M4 BMW render returned an invalid result" +
                                 validation_report_text(result_validation)};
    }
    const auto *success = std::get_if<contract::RenderSuccess>(&result);
    if (success == nullptr) {
        throw_render_outcome(result);
    }
    if (sink.state() != artifacts::DirectoryRenderSinkState::committed) {
        throw std::runtime_error{
            "successful M4 BMW render did not commit its directory transaction"};
    }
    if (success->reached_target.has_value() ||
        success->held_speed_operating_point.has_value() ||
        !success->inertial_dyno.has_value()) {
        throw std::runtime_error{
            "successful M4 BMW inertial-dyno render has the wrong operating evidence"};
    }

    const auto &operating = *success->inertial_dyno;
    if (!operating.first_target_reached_frame_index.has_value()) {
        throw std::runtime_error{
            "M4 BMW inertial-dyno pull did not reach its target speed"};
    }
    const auto audition = output_directory / "audio/master.reference.audition.wav";
    const double command_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      command_started)
            .count();
    std::cout << std::setprecision(17) << "output=" << output_directory.string() << '\n'
              << "audition=" << audition.string() << '\n'
              << "scenario=" << request.scenario.scenario_id << '\n'
              << "start_engine_speed_rpm=" << operating.start_engine_speed_rpm << '\n'
              << "release_engine_speed_rpm=" << operating.release_engine_speed_rpm
              << '\n'
              << "target_engine_speed_rpm=" << operating.target_engine_speed_rpm << '\n'
              << "end_engine_speed_rpm=" << operating.end_engine_speed_rpm << '\n'
              << "minimum_engine_speed_rpm=" << operating.minimum_engine_speed_rpm
              << '\n'
              << "maximum_engine_speed_rpm=" << operating.maximum_engine_speed_rpm
              << '\n'
              << "release_frame_index=" << operating.release_frame_index << '\n'
              << "first_target_reached_frame_index="
              << *operating.first_target_reached_frame_index << '\n'
              << "end_frame_index=" << operating.end_frame_index << '\n'
              << "net_shaft_work_j=" << operating.energy_balance.net_shaft_work_j
              << '\n'
              << "passive_brake_absorbed_work_j="
              << operating.energy_balance.passive_brake_absorbed_work_j << '\n'
              << "kinetic_energy_change_j="
              << operating.energy_balance.kinetic_energy_change_j << '\n'
              << "energy_residual_j=" << operating.energy_balance.residual_j << '\n'
              << "render_seconds=" << render_seconds << '\n'
              << "total_command_seconds=" << command_seconds << '\n';
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "M4 BMW listening render failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
