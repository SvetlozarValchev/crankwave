#include "reference/bmw_p18_render_specification.hpp"

#include "engine_sim_offline/artifacts/directory_render_sink.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_held_idle_low_load_request.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_held_regression_request.hpp"
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

enum class ListeningMode : std::uint8_t {
    held_rpm1500_throttle0p85,
    held_rpm3000_throttle0p25,
    held_rpm3000_throttle0p85,
    held_rpm6500_throttle0p85,
    held_idle_region_rpm700_throttle0,
    held_low_load_rpm1500_throttle0p10,
    inertial,
};

struct ListeningRequest {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;
};

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

[[nodiscard]] ListeningMode parse_listening_mode(std::string_view mode) {
    if (mode == "held-rpm1500-throttle0p85") {
        return ListeningMode::held_rpm1500_throttle0p85;
    }
    if (mode == "held-rpm3000-throttle0p25") {
        return ListeningMode::held_rpm3000_throttle0p25;
    }
    if (mode == "held-rpm3000-throttle0p85") {
        return ListeningMode::held_rpm3000_throttle0p85;
    }
    if (mode == "held-rpm6500-throttle0p85") {
        return ListeningMode::held_rpm6500_throttle0p85;
    }
    if (mode == "held-idle-region-rpm700-throttle0") {
        return ListeningMode::held_idle_region_rpm700_throttle0;
    }
    if (mode == "held-low-load-rpm1500-throttle0p10") {
        return ListeningMode::held_low_load_rpm1500_throttle0p10;
    }
    if (mode == "inertial") {
        return ListeningMode::inertial;
    }
    throw std::invalid_argument{
        "listening mode must be exactly one of "
        "'held-rpm1500-throttle0p85', 'held-rpm3000-throttle0p25', "
        "'held-rpm3000-throttle0p85', 'held-rpm6500-throttle0p85', "
        "'held-idle-region-rpm700-throttle0', "
        "'held-low-load-rpm1500-throttle0p10', or 'inertial'"};
}

[[nodiscard]] std::string_view listening_mode_name(ListeningMode mode) noexcept {
    switch (mode) {
    case ListeningMode::held_rpm1500_throttle0p85:
        return "held-rpm1500-throttle0p85";
    case ListeningMode::held_rpm3000_throttle0p25:
        return "held-rpm3000-throttle0p25";
    case ListeningMode::held_rpm3000_throttle0p85:
        return "held-rpm3000-throttle0p85";
    case ListeningMode::held_rpm6500_throttle0p85:
        return "held-rpm6500-throttle0p85";
    case ListeningMode::held_idle_region_rpm700_throttle0:
        return "held-idle-region-rpm700-throttle0";
    case ListeningMode::held_low_load_rpm1500_throttle0p10:
        return "held-low-load-rpm1500-throttle0p10";
    case ListeningMode::inertial:
        return "inertial";
    }
    std::abort();
}

[[nodiscard]] bool is_held_mode(ListeningMode mode) noexcept {
    return mode != ListeningMode::inertial;
}

[[nodiscard]] bool is_held_idle_low_load_mode(ListeningMode mode) noexcept {
    return mode == ListeningMode::held_idle_region_rpm700_throttle0 ||
           mode == ListeningMode::held_low_load_rpm1500_throttle0p10;
}

[[nodiscard]] std::size_t held_point_index(ListeningMode mode) {
    switch (mode) {
    case ListeningMode::held_rpm1500_throttle0p85:
        return 0U;
    case ListeningMode::held_rpm3000_throttle0p25:
        return 1U;
    case ListeningMode::held_rpm3000_throttle0p85:
        return 2U;
    case ListeningMode::held_rpm6500_throttle0p85:
        return 3U;
    case ListeningMode::held_idle_region_rpm700_throttle0:
    case ListeningMode::held_low_load_rpm1500_throttle0p10:
    case ListeningMode::inertial:
        break;
    }
    throw std::logic_error{"non-regression mode has no held-regression point"};
}

[[nodiscard]] std::size_t held_idle_low_load_point_index(ListeningMode mode) {
    switch (mode) {
    case ListeningMode::held_idle_region_rpm700_throttle0:
        return 0U;
    case ListeningMode::held_low_load_rpm1500_throttle0p10:
        return 1U;
    case ListeningMode::held_rpm1500_throttle0p85:
    case ListeningMode::held_rpm3000_throttle0p25:
    case ListeningMode::held_rpm3000_throttle0p85:
    case ListeningMode::held_rpm6500_throttle0p85:
    case ListeningMode::inertial:
        break;
    }
    throw std::logic_error{"non-idle-low-load mode has no held idle/low-load point"};
}

[[nodiscard]] ListeningRequest make_listening_request(ListeningMode mode) {
    if (is_held_idle_low_load_mode(mode)) {
        auto result = profiles::make_bmw_m52b28_held_idle_low_load_request_set();
        if (auto *request_set =
                std::get_if<profiles::BmwM52b28HeldIdleLowLoadRequestSet>(&result)) {
            auto &request = request_set->at(held_idle_low_load_point_index(mode));
            return {
                std::move(request.engine),
                std::move(request.scenario),
                std::move(request.provenance),
            };
        }
        throw std::runtime_error{
            "canonical M4 BMW held idle/low-load request-set construction failed" +
            validation_report_text(std::get<contract::ValidationReport>(result))};
    }

    if (is_held_mode(mode)) {
        auto result = profiles::make_bmw_m52b28_held_regression_request_set();
        if (auto *request_set =
                std::get_if<profiles::BmwM52b28HeldRegressionRequestSet>(&result)) {
            auto &request = request_set->at(held_point_index(mode));
            return {
                std::move(request.engine),
                std::move(request.scenario),
                std::move(request.provenance),
            };
        }
        throw std::runtime_error{
            "canonical M4 BMW held-regression request-set construction failed" +
            validation_report_text(std::get<contract::ValidationReport>(result))};
    }

    auto result = profiles::make_bmw_m52b28_inertial_dyno_listening_request();
    if (auto *request =
            std::get_if<profiles::BmwM52b28InertialDynoListeningRequest>(&result)) {
        return {
            std::move(request->engine),
            std::move(request->scenario),
            std::move(request->provenance),
        };
    }
    throw std::runtime_error{
        "canonical M4 BMW inertial listening request construction failed" +
        validation_report_text(std::get<contract::ValidationReport>(result))};
}

[[noreturn]] void throw_render_outcome(const contract::RenderResult &result,
                                       ListeningMode mode) {
    if (const auto *failure = std::get_if<contract::RenderFailure>(&result)) {
        throw std::runtime_error{"M4 BMW " + std::string{listening_mode_name(mode)} +
                                 " render failed (" + failure->context.detail_code +
                                 "): " + failure->context.state_summary +
                                 validation_report_text(failure->validation)};
    }
    const auto &unreachable = std::get<contract::UnreachableTarget>(result);
    throw std::runtime_error{"M4 BMW " + std::string{listening_mode_name(mode)} +
                             " request unexpectedly returned unreachable-target (" +
                             unreachable.context.detail_code +
                             "): " + unreachable.context.state_summary};
}

int run(int argc, char **argv) {
    if (argc != 4) {
        throw std::invalid_argument{
            "usage: engine-sim-offline-m4-bmw-listening "
            "<held-rpm1500-throttle0p85|held-rpm3000-throttle0p25|"
            "held-rpm3000-throttle0p85|held-rpm6500-throttle0p85|"
            "held-idle-region-rpm700-throttle0|"
            "held-low-load-rpm1500-throttle0p10|inertial> "
            "<fixture-root> <new-output-directory>"};
    }
    const auto mode = parse_listening_mode(argv[1]);
    const std::filesystem::path fixture_root{argv[2]};
    const std::filesystem::path output_directory{argv[3]};
    const auto publication_name = output_directory.filename().string();
    auto publication_root = output_directory.parent_path();
    if (output_directory.empty() || publication_name.empty()) {
        throw std::invalid_argument{
            "new output directory must end in one publication-name component"};
    }
    if (publication_name.front() == '-') {
        throw std::invalid_argument{"output publication name must not begin with '-'"};
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
    auto request = make_listening_request(mode);
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
        throw_render_outcome(result, mode);
    }
    if (sink.state() != artifacts::DirectoryRenderSinkState::committed) {
        throw std::runtime_error{
            "successful M4 BMW render did not commit its directory transaction"};
    }
    const auto audition = output_directory / "audio/master.reference.audition.wav";
    const double command_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      command_started)
            .count();
    std::cout << std::setprecision(17) << "mode=" << listening_mode_name(mode) << '\n'
              << "output=" << output_directory.string() << '\n'
              << "audition=" << audition.string() << '\n'
              << "scenario=" << request.scenario.scenario_id << '\n';

    if (is_held_mode(mode)) {
        if (success->reached_target.has_value() ||
            !success->held_speed_operating_point.has_value() ||
            success->inertial_dyno.has_value()) {
            throw std::runtime_error{
                "successful M4 BMW held render has the wrong operating evidence"};
        }
        const auto &operating = *success->held_speed_operating_point;
        const auto &sample = operating.sampling;
        const auto &block = operating.reported_block();
        std::cout << "engine_speed_rpm=" << operating.conditions.engine_speed_rpm
                  << '\n'
                  << "throttle_01=" << operating.conditions.throttle_01 << '\n'
                  << "fixed_preparation_horizon_s="
                  << sample.fixed_preparation_horizon_s << '\n'
                  << "trailing_complete_cycle_count="
                  << sample.trailing_complete_cycle_count << '\n'
                  << "first_completed_cycle_ordinal="
                  << block.cycles.first_completed_cycle_ordinal << '\n'
                  << "last_completed_cycle_ordinal="
                  << block.cycles.last_completed_cycle_ordinal << '\n'
                  << "indicated_gas_mean_torque_nm="
                  << block.cycle_mean_torque.indicated_gas.value_nm << '\n'
                  << "aggregate_loss_mean_torque_nm="
                  << block.cycle_mean_torque.aggregate_loss.value_nm << '\n'
                  << "net_shaft_mean_torque_nm="
                  << block.cycle_mean_torque.net_shaft.value_nm << '\n'
                  << "net_bmep_pa=" << block.net_bmep_pa << '\n'
                  << "mean_power_w=" << block.mean_power_w << '\n';
    } else {
        if (success->reached_target.has_value() ||
            success->held_speed_operating_point.has_value() ||
            !success->inertial_dyno.has_value()) {
            throw std::runtime_error{
                "successful M4 BMW inertial-dyno render has the wrong operating "
                "evidence"};
        }
        const auto &operating = *success->inertial_dyno;
        if (!operating.first_target_reached_frame_index.has_value()) {
            throw std::runtime_error{
                "M4 BMW inertial-dyno pull did not reach its target speed"};
        }
        std::cout
            << "start_engine_speed_rpm=" << operating.start_engine_speed_rpm << '\n'
            << "release_engine_speed_rpm=" << operating.release_engine_speed_rpm << '\n'
            << "target_engine_speed_rpm=" << operating.target_engine_speed_rpm << '\n'
            << "end_engine_speed_rpm=" << operating.end_engine_speed_rpm << '\n'
            << "minimum_engine_speed_rpm=" << operating.minimum_engine_speed_rpm << '\n'
            << "maximum_engine_speed_rpm=" << operating.maximum_engine_speed_rpm << '\n'
            << "release_frame_index=" << operating.release_frame_index << '\n'
            << "first_target_reached_frame_index="
            << *operating.first_target_reached_frame_index << '\n'
            << "end_frame_index=" << operating.end_frame_index << '\n'
            << "net_shaft_work_j=" << operating.energy_balance.net_shaft_work_j << '\n'
            << "passive_brake_absorbed_work_j="
            << operating.energy_balance.passive_brake_absorbed_work_j << '\n'
            << "kinetic_energy_change_j="
            << operating.energy_balance.kinetic_energy_change_j << '\n'
            << "energy_residual_j=" << operating.energy_balance.residual_j << '\n';
    }

    std::cout << "render_seconds=" << render_seconds << '\n'
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
