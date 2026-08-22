#include "compile/compiled_scenario_view.hpp"
#include "crankwave/authoring/parse.hpp"
#include "crankwave/compile.hpp"
#include "crankwave/request_identity.hpp"
#include "crankwave/session.hpp"
#include "simulation/centered_slider_crank_equivalent_inertia.hpp"
#include "simulation/free_engine_method_registry.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace authoring = crankwave::authoring;
namespace compile = crankwave::compile;
namespace compile_detail = crankwave::compile::detail;
namespace contract = crankwave::contract;
namespace identity = crankwave::identity;
namespace simulation = crankwave::simulation;

constexpr std::string_view kFixtureDirectory =
    "data/engines/cocentered-split-crank-v-twin";

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(const double left, const double right,
                        const double tolerance = 1.0e-12) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] std::string diagnostics(const authoring::DiagnosticReport &report) {
    std::string result;
    for (const auto &diagnostic : report.diagnostics) {
        if (!result.empty()) {
            result += "; ";
        }
        result += diagnostic.json_pointer + ": " + diagnostic.message;
    }
    return result.empty() ? "no diagnostic detail" : result;
}

template <class Value, class Report>
[[nodiscard]] Value require(std::variant<Value, Report> result,
                            const std::string_view context) {
    if (const auto *report = std::get_if<Report>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error{"could not open " + path.string()};
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    const auto text = read_text(path);
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

struct OwnedAsset {
    compile::AssetKind kind = compile::AssetKind::audio;
    std::string id;
    std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<OwnedAsset>
load_assets(const authoring::EnginePackageDocument &document,
            const std::filesystem::path &engine_path) {
    std::vector<OwnedAsset> result;
    result.reserve(document.presentation.assets.size() +
                   document.engine.accessory_configurations.size());
    for (const auto &asset : document.presentation.assets) {
        result.push_back({compile::AssetKind::audio, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    for (const auto &asset : document.engine.accessory_configurations) {
        result.push_back({compile::AssetKind::accessory_configuration, asset.id.value,
                          read_bytes(engine_path.parent_path() / asset.uri)});
    }
    return result;
}

[[nodiscard]] std::vector<compile::AssetPayloadView>
asset_views(const std::vector<OwnedAsset> &assets) {
    std::vector<compile::AssetPayloadView> result;
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.push_back({asset.kind, asset.id, asset.bytes});
    }
    return result;
}

struct FixtureVariant {
    std::filesystem::path engine_path;
    authoring::EnginePackageDocument engine_document;
    authoring::ScenarioDocument scenario_document;
    compile::CompiledScenario compiled_scenario;
};

[[nodiscard]] compile::CompiledScenario
compile_variant(const authoring::EnginePackageDocument &engine_document,
                const authoring::ScenarioDocument &scenario_document,
                const std::filesystem::path &engine_path) {
    const auto references =
        authoring::validate_scenario_references(scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{
            "multiple-crankshaft execution fixture cross-document validation "
            "failed: " +
            diagnostics(references)};
    }
    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    auto engine = require(compile::compile_engine(engine_document, views),
                          "multiple-crankshaft execution fixture engine compilation "
                          "failed");
    return require(compile::compile_scenario(engine, scenario_document),
                   "multiple-crankshaft execution fixture scenario compilation "
                   "failed");
}

[[nodiscard]] FixtureVariant
load_variant(const std::filesystem::path &repository_root,
             const std::string_view engine_filename,
             const std::string_view scenario_filename = "prescribed-1500rpm.json") {
    const auto fixture_root = repository_root / kFixtureDirectory;
    const auto engine_path = fixture_root / engine_filename;
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "multiple-crankshaft execution fixture engine parse failed");
    auto scenario_document =
        require(authoring::parse_scenario_document(
                    read_text(fixture_root / "scenarios" / scenario_filename)),
                "multiple-crankshaft execution fixture scenario parse failed");
    auto scenario = compile_variant(engine_document, scenario_document, engine_path);
    return {engine_path, std::move(engine_document), std::move(scenario_document),
            std::move(scenario)};
}

[[nodiscard]] compile_detail::CompiledScenarioInputsView
inputs(const compile::CompiledScenario &scenario) {
    return compile_detail::CompiledScenarioViewAccess::inputs(scenario);
}

[[nodiscard]] const contract::ResolutionRecord &
resolution_record(const compile::CompiledScenario &scenario,
                  const std::string_view parameter_path) {
    const auto resolved = inputs(scenario);
    const auto found =
        std::ranges::find(resolved.scenario.combined_provenance.resolutions,
                          parameter_path, &contract::ResolutionRecord::parameter_path);
    if (found == resolved.scenario.combined_provenance.resolutions.end()) {
        throw std::runtime_error{"resolved scenario omitted provenance for '" +
                                 std::string{parameter_path} + "'"};
    }
    return *found;
}

[[nodiscard]] const contract::LowOrderOperatingPointV1Profile &
profile(const contract::EngineSpec &engine) {
    return std::get<contract::LowOrderOperatingPointV1Profile>(engine.physics_profile);
}

[[nodiscard]] contract::CrankshaftId crank_id(const contract::EngineSpec &engine,
                                              const std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.crankshafts, semantic_id,
                          [](const auto &crankshaft) -> std::string_view {
                              return crankshaft.semantic_id.value;
                          });
    if (found == engine.crankshafts.end()) {
        throw std::runtime_error{"resolved engine omitted crankshaft '" +
                                 std::string{semantic_id} + "'"};
    }
    return found->id;
}

[[nodiscard]] const contract::CylinderSpec &
cylinder(const contract::EngineSpec &engine, const std::string_view semantic_id) {
    const auto found = std::ranges::find(
        engine.cylinders, semantic_id,
        [](const auto &value) -> std::string_view { return value.semantic_id.value; });
    if (found == engine.cylinders.end()) {
        throw std::runtime_error{"resolved engine omitted cylinder '" +
                                 std::string{semantic_id} + "'"};
    }
    return *found;
}

[[nodiscard]] const contract::LegacyCylinderAssembly &
core_cylinder(const contract::LowOrderOperatingPointV1Profile &physics,
              const contract::CylinderId cylinder_id) {
    const auto found =
        std::ranges::find(physics.core.mechanism.cylinders, cylinder_id,
                          [](const auto &value) { return value.topology.cylinder_id; });
    if (found == physics.core.mechanism.cylinders.end()) {
        throw std::runtime_error{"resolved core omitted cylinder binding"};
    }
    return *found;
}

[[nodiscard]] identity::SimulationRequestIdentityEncoding
request_identity(const compile::CompiledScenario &scenario) {
    const auto resolved = inputs(scenario);
    auto encoded = identity::encode_simulation_request_identity_v7(
        resolved.engine.engine, resolved.scenario.scenario,
        resolved.scenario.random_plan, resolved.scenario.combined_provenance.bundle);
    if (const auto *error =
            std::get_if<identity::SimulationRequestIdentityError>(&encoded)) {
        throw std::runtime_error{
            "multiple-crankshaft execution fixture request identity failed: " +
            error->detail_code + ": " + error->message};
    }
    return std::get<identity::SimulationRequestIdentityEncoding>(std::move(encoded));
}

[[nodiscard]] crankwave::EngineSession
require_session(const compile::CompiledScenario &scenario) {
    auto result = crankwave::create_engine_session(
        scenario, crankwave::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<crankwave::EngineSessionError>(&result)) {
        throw std::runtime_error{
            "multiple-crankshaft execution fixture session creation failed: " +
            error->detail_code + ": " + error->message};
    }
    return std::get<crankwave::EngineSession>(std::move(result));
}

[[nodiscard]] bool same_f64(const double left, const double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool same_quantity(const contract::QuantityValue &left,
                                 const contract::QuantityValue &right) noexcept {
    return same_f64(left.value, right.value) &&
           left.availability == right.availability &&
           left.completeness == right.completeness &&
           left.unavailable_reason == right.unavailable_reason;
}

[[nodiscard]] bool same_torque_value(const contract::TorqueValueNm &left,
                                     const contract::TorqueValueNm &right) noexcept {
    return same_f64(left.value_nm, right.value_nm) &&
           left.availability == right.availability &&
           left.completeness == right.completeness &&
           left.unavailable_reason == right.unavailable_reason &&
           left.included_terms == right.included_terms &&
           left.omitted_terms == right.omitted_terms;
}

[[nodiscard]] bool same_torque(const contract::TorqueTelemetry &left,
                               const contract::TorqueTelemetry &right) noexcept {
    return same_torque_value(left.instantaneous_indicated_gas,
                             right.instantaneous_indicated_gas) &&
           same_torque_value(left.pumping_partition, right.pumping_partition) &&
           same_torque_value(left.friction_pump_and_accessory,
                             right.friction_pump_and_accessory) &&
           same_torque_value(left.starter, right.starter) &&
           same_torque_value(left.instantaneous_net_shaft,
                             right.instantaneous_net_shaft) &&
           same_torque_value(left.cycle_mean_net_shaft, right.cycle_mean_net_shaft) &&
           same_torque_value(left.actuator, right.actuator) &&
           same_torque_value(left.dyno_reaction, right.dyno_reaction) &&
           same_quantity(left.cycle_work_j, right.cycle_work_j) &&
           same_quantity(left.net_bmep_pa, right.net_bmep_pa) &&
           same_quantity(left.instantaneous_power_w, right.instantaneous_power_w) &&
           same_quantity(left.cycle_mean_power_w, right.cycle_mean_power_w);
}

[[nodiscard]] bool
same_capture_sample(const contract::EngineCaptureSample &left,
                    const contract::EngineCaptureSample &right) noexcept {
    return left.step_end_index == right.step_end_index &&
           left.validity == right.validity &&
           same_f64(left.theta_rad, right.theta_rad) &&
           same_f64(left.theta_cycle_rad, right.theta_cycle_rad) &&
           same_f64(left.angular_speed_rad_s, right.angular_speed_rad_s) &&
           same_f64(left.angular_acceleration_rad_s2,
                    right.angular_acceleration_rad_s2) &&
           same_f64(left.engine_speed_rpm, right.engine_speed_rpm) &&
           same_f64(left.requested_throttle_01, right.requested_throttle_01) &&
           same_f64(left.resolved_engine_throttle_01,
                    right.resolved_engine_throttle_01) &&
           same_f64(left.intake_plate_position_01, right.intake_plate_position_01) &&
           same_f64(left.main_flow_multiplier_01, right.main_flow_multiplier_01) &&
           left.ignition_enabled == right.ignition_enabled &&
           left.fuel_enabled == right.fuel_enabled &&
           left.starter_enabled == right.starter_enabled &&
           left.dyno_enabled == right.dyno_enabled &&
           left.limiter_enabled == right.limiter_enabled &&
           left.limiter_cut_active == right.limiter_cut_active &&
           same_f64(left.requested_external_resisting_torque_nm,
                    right.requested_external_resisting_torque_nm) &&
           same_torque(left.torque, right.torque);
}

[[nodiscard]] bool
same_capture_trace(const std::vector<contract::EngineCaptureSample> &left,
                   const std::vector<contract::EngineCaptureSample> &right) noexcept {
    return left.size() == right.size() &&
           std::ranges::equal(left, right, same_capture_sample);
}

[[nodiscard]] std::string
validation_diagnostics(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        if (!result.empty()) {
            result += "; ";
        }
        result += issue.path + ": " + issue.message;
    }
    return result.empty() ? "no validation detail" : result;
}

struct DynamicCapture {
    std::vector<contract::EngineCaptureSample> frames;
    simulation::LowOrderCaptureCompleted completion;
};

[[nodiscard]] DynamicCapture
capture_dynamic_motion(const compile::CompiledScenario &scenario) {
    const auto resolved = inputs(scenario);
    const auto encoded = request_identity(scenario);
    const auto frame_count =
        contract::resolve_frame_index(resolved.scenario.scenario.total_duration_s.value,
                                      resolved.scenario.scenario.rates.physics);
    expect(frame_count.has_value() && *frame_count != 0U,
           "multiple-crankshaft dynamic fixture has no exact physics horizon");

    auto compiled = simulation::compile_low_order_capture_session(
        resolved.engine.engine, resolved.scenario.scenario,
        resolved.scenario.random_plan, encoded.sha256,
        simulation::LowOrderExecutionExtent::finite_scenario(*frame_count));
    if (const auto *report = std::get_if<contract::ValidationReport>(&compiled)) {
        throw std::runtime_error{
            "multiple-crankshaft dynamic capture compilation failed: " +
            validation_diagnostics(*report)};
    }
    auto session = std::get<simulation::LowOrderCaptureSession>(std::move(compiled));
    DynamicCapture capture;
    while (true) {
        auto result =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                expect(block.clock().first_sample_index == capture.frames.size(),
                       "multiple-crankshaft dynamic capture lost contiguous frame "
                       "order");
                capture.frames.insert(capture.frames.end(), block.engine().begin(),
                                      block.engine().end());
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
            throw std::runtime_error{
                "multiple-crankshaft dynamic capture faulted: " + failure->detail_code +
                ": " + failure->state_summary};
        }
        if (const auto *completed =
                std::get_if<simulation::LowOrderCaptureCompleted>(&result)) {
            capture.completion = *completed;
            expect(capture.frames.size() == *frame_count &&
                       capture.completion.sample_count == *frame_count,
                   "multiple-crankshaft dynamic capture did not complete its exact "
                   "horizon");
            return capture;
        }
    }
}

enum class ExpectedSessionSidecar : std::uint8_t {
    none,
    held_dyno,
    free_vehicle,
};

struct SessionExpectation {
    crankwave::EngineMotionMode motion_mode;
    ExpectedSessionSidecar sidecar;
};

struct SessionObservation {
    std::vector<crankwave::EngineTelemetryFrame> telemetry;
    std::vector<std::byte> audition_pcm;
    crankwave::EngineSessionCompleted completion;
};

[[nodiscard]] bool
telemetry_has_no_wrong_sidecar(const crankwave::EngineTelemetryFrame &frame,
                               const ExpectedSessionSidecar expected) noexcept {
    switch (expected) {
    case ExpectedSessionSidecar::none:
        return !frame.held_dyno.has_value() && !frame.free_vehicle.has_value();
    case ExpectedSessionSidecar::held_dyno:
        return !frame.free_vehicle.has_value();
    case ExpectedSessionSidecar::free_vehicle:
        return !frame.held_dyno.has_value();
    }
    return false;
}

[[nodiscard]] bool
telemetry_has_expected_sidecar(const crankwave::EngineTelemetryFrame &frame,
                               const ExpectedSessionSidecar expected) noexcept {
    switch (expected) {
    case ExpectedSessionSidecar::none:
        return true;
    case ExpectedSessionSidecar::held_dyno:
        return frame.held_dyno.has_value();
    case ExpectedSessionSidecar::free_vehicle:
        return frame.free_vehicle.has_value();
    }
    return false;
}

[[nodiscard]] SessionObservation run_session(const compile::CompiledScenario &scenario,
                                             const SessionExpectation expectation) {
    auto session = require_session(scenario);
    expect(session.descriptor().motion_mode == expectation.motion_mode,
           "multiple-crankshaft fixture compiled with the wrong motion mode");

    SessionObservation observation;
    bool observed_nonzero = false;
    bool observed_sidecar = expectation.sidecar == ExpectedSessionSidecar::none;
    while (true) {
        auto result = session.process_block();
        if (const auto *block =
                std::get_if<crankwave::EngineSessionBlockView>(&result)) {
            for (const auto &frame : block->telemetry()) {
                expect(telemetry_has_no_wrong_sidecar(frame, expectation.sidecar),
                       "multiple-crankshaft fixture published the wrong telemetry "
                       "sidecar");
                observed_sidecar = observed_sidecar || telemetry_has_expected_sidecar(
                                                           frame, expectation.sidecar);
            }
            observation.telemetry.insert(observation.telemetry.end(),
                                         block->telemetry().begin(),
                                         block->telemetry().end());
            const auto bus = std::ranges::find(
                block->audio_buses(),
                crankwave::EngineAudioBusKind::engine_audition_master,
                [](const auto &value) { return value.descriptor.kind; });
            expect(bus != block->audio_buses().end() &&
                       std::ranges::all_of(
                           bus->samples,
                           [](const float sample) { return std::isfinite(sample); }),
                   "multiple-crankshaft fixture emitted invalid audition PCM");
            observed_nonzero =
                observed_nonzero ||
                std::ranges::any_of(bus->samples,
                                    [](const float sample) { return sample != 0.0F; });
            const auto bytes = std::as_bytes(bus->samples);
            observation.audition_pcm.insert(observation.audition_pcm.end(),
                                            bytes.begin(), bytes.end());
            continue;
        }
        if (const auto *error =
                std::get_if<crankwave::EngineSessionError>(&result)) {
            throw std::runtime_error{"multiple-crankshaft session faulted: " +
                                     error->detail_code + ": " + error->message};
        }
        observation.completion =
            std::get<crankwave::EngineSessionCompleted>(result);
        expect(observed_nonzero && observed_sidecar && !observation.telemetry.empty() &&
                   !observation.audition_pcm.empty(),
               "multiple-crankshaft session did not publish its expected telemetry "
               "and nonzero PCM");
        return observation;
    }
}

[[nodiscard]] bool
same_held_dyno(const crankwave::EngineHeldDynoTelemetry &left,
               const crankwave::EngineHeldDynoTelemetry &right) noexcept {
    return same_f64(left.target_engine_speed_rpm, right.target_engine_speed_rpm) &&
           same_f64(left.maximum_absorbing_torque_nm,
                    right.maximum_absorbing_torque_nm) &&
           same_f64(left.maximum_driving_torque_nm, right.maximum_driving_torque_nm) &&
           same_f64(left.required_actuator_torque_nm,
                    right.required_actuator_torque_nm) &&
           same_f64(left.applied_actuator_torque_nm,
                    right.applied_actuator_torque_nm) &&
           left.disposition == right.disposition;
}

[[nodiscard]] bool same_optional_f64(const std::optional<double> &left,
                                     const std::optional<double> &right) noexcept {
    return left.has_value() == right.has_value() &&
           (!left.has_value() || same_f64(*left, *right));
}

[[nodiscard]] bool same_free_vehicle(
    const crankwave::EngineFreeVehicleTelemetry &left,
    const crankwave::EngineFreeVehicleTelemetry &right) noexcept {
    return same_f64(left.vehicle_speed_m_s, right.vehicle_speed_m_s) &&
           same_f64(left.vehicle_distance_m, right.vehicle_distance_m) &&
           left.selected_forward_gear_ordinal == right.selected_forward_gear_ordinal &&
           same_f64(left.clutch_engagement_01, right.clutch_engagement_01) &&
           same_f64(left.service_brake_application_01,
                    right.service_brake_application_01) &&
           left.clutch_disposition == right.clutch_disposition &&
           same_f64(left.clutch_torque_capacity_nm, right.clutch_torque_capacity_nm) &&
           same_f64(left.applied_average_clutch_torque_on_engine_nm,
                    right.applied_average_clutch_torque_on_engine_nm) &&
           same_optional_f64(left.final_clutch_slip_rad_s,
                             right.final_clutch_slip_rad_s) &&
           left.road_load_disposition == right.road_load_disposition &&
           same_f64(left.requested_road_load_force_n,
                    right.requested_road_load_force_n) &&
           same_f64(left.applied_average_road_load_force_n,
                    right.applied_average_road_load_force_n);
}

[[nodiscard]] bool same_session_telemetry(
    const std::vector<crankwave::EngineTelemetryFrame> &left,
    const std::vector<crankwave::EngineTelemetryFrame> &right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto &a = left[index];
        const auto &b = right[index];
        const bool same_held =
            a.held_dyno.has_value() == b.held_dyno.has_value() &&
            (!a.held_dyno.has_value() || same_held_dyno(*a.held_dyno, *b.held_dyno));
        const bool same_vehicle =
            a.free_vehicle.has_value() == b.free_vehicle.has_value() &&
            (!a.free_vehicle.has_value() ||
             same_free_vehicle(*a.free_vehicle, *b.free_vehicle));
        if (a.physics_step_end != b.physics_step_end || !same_held || !same_vehicle ||
            !same_capture_sample(a.engine, b.engine)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
same_completion(const crankwave::EngineSessionCompleted &left,
                const crankwave::EngineSessionCompleted &right) noexcept {
    return left.physics_frame_count == right.physics_frame_count &&
           left.delivery_frame_count == right.delivery_frame_count &&
           left.block_count == right.block_count &&
           left.live_controls_accepted == right.live_controls_accepted &&
           left.held_speed_operating_point == right.held_speed_operating_point &&
           left.inertial_dyno == right.inertial_dyno;
}

void verify_exact_session_ab(const FixtureVariant &a, const FixtureVariant &b,
                             const SessionExpectation expectation) {
    const auto a_identity = request_identity(a.compiled_scenario);
    const auto b_identity = request_identity(b.compiled_scenario);
    expect(a_identity.sha256 != b_identity.sha256 &&
               a_identity.bytes != b_identity.bytes,
           "one-crank and two-crank execution fixtures produced one canonical "
           "request identity");

    const auto a_observation = run_session(a.compiled_scenario, expectation);
    const auto b_observation = run_session(b.compiled_scenario, expectation);
    expect(same_session_telemetry(a_observation.telemetry, b_observation.telemetry) &&
               a_observation.audition_pcm == b_observation.audition_pcm &&
               same_completion(a_observation.completion, b_observation.completion),
           "aggregate-equivalent crank decomposition changed exact session "
           "telemetry, audition PCM, or completion");
}

void verify_authored_ab_delta(const FixtureVariant &a, const FixtureVariant &b) {
    expect(a.scenario_document == b.scenario_document,
           "multiple-crankshaft prescribed fixture variants did not share one "
           "request");
    expect(a.engine_document.engine.crankshafts.size() == 1U &&
               b.engine_document.engine.crankshafts.size() == 2U &&
               a.engine_document.engine.journals.size() == 2U &&
               b.engine_document.engine.journals.size() == 2U &&
               a.engine_document.engine.output_crankshaft.value == "crank.output" &&
               b.engine_document.engine.output_crankshaft.value == "crank.output",
           "multiple-crankshaft prescribed fixture crank/journal shape changed");

    auto normalized_b = b.engine_document;
    normalized_b.engine.crankshafts = a.engine_document.engine.crankshafts;
    normalized_b.engine.journals = a.engine_document.engine.journals;
    expect(normalized_b == a.engine_document,
           "multiple-crankshaft prescribed fixture documents differ outside crank "
           "decomposition and journal "
           "ownership");
}

void verify_resolved_ab_invariants(const FixtureVariant &a, const FixtureVariant &b) {
    const auto a_inputs = inputs(a.compiled_scenario);
    const auto b_inputs = inputs(b.compiled_scenario);
    const auto &a_engine = a_inputs.engine.engine;
    const auto &b_engine = b_inputs.engine.engine;
    const auto &a_profile = profile(a_engine);
    const auto &b_profile = profile(b_engine);
    const auto output_id = crank_id(b_engine, "crank.output");
    const auto secondary_id = crank_id(b_engine, "crank.secondary");
    const auto &a_front = cylinder(a_engine, "cylinder.front");
    const auto &a_rear = cylinder(a_engine, "cylinder.rear");
    const auto &b_front = cylinder(b_engine, "cylinder.front");
    const auto &b_rear = cylinder(b_engine, "cylinder.rear");

    expect(a_engine.crankshafts.size() == 1U &&
               a_engine.output_crankshaft_id == a_engine.crankshafts.front().id &&
               a_front.crankshaft_id == a_engine.output_crankshaft_id &&
               a_rear.crankshaft_id == a_engine.output_crankshaft_id,
           "multiple-crankshaft prescribed fixture A lost its one-crank/two-journal "
           "ownership");
    expect(b_engine.crankshafts.size() == 2U &&
               b_engine.crankshafts[0].semantic_id.value == "crank.output" &&
               b_engine.crankshafts[1].semantic_id.value == "crank.secondary" &&
               b_engine.output_crankshaft_id == output_id &&
               output_id != secondary_id && b_front.crankshaft_id == output_id &&
               b_rear.crankshaft_id == secondary_id,
           "multiple-crankshaft prescribed fixture B collapsed explicit "
           "cylinder-to-crank identity");
    expect(core_cylinder(b_profile, b_front.id).topology.crankshaft_id == output_id &&
               core_cylinder(b_profile, b_rear.id).topology.crankshaft_id ==
                   secondary_id,
           "multiple-crankshaft prescribed fixture B core topology lost exact "
           "crankshaft bindings");

    const auto *a_crank = contract::find_output_crank(a_profile.core.mechanism);
    const auto *b_output = contract::find_crank(b_profile.core.mechanism, output_id);
    const auto *b_secondary =
        contract::find_crank(b_profile.core.mechanism, secondary_id);
    expect(a_crank != nullptr && b_output != nullptr && b_secondary != nullptr,
           "multiple-crankshaft prescribed fixture omitted an authored crank "
           "assembly");
    expect(near(a_crank->crankshaft_mass_kg.value,
                b_output->crankshaft_mass_kg.value +
                    b_secondary->crankshaft_mass_kg.value) &&
               near(a_crank->flywheel_mass_kg.value,
                    b_output->flywheel_mass_kg.value +
                        b_secondary->flywheel_mass_kg.value) &&
               near(a_crank->authored_crank_inertia_kg_m2.value,
                    b_output->authored_crank_inertia_kg_m2.value +
                        b_secondary->authored_crank_inertia_kg_m2.value) &&
               near(a_crank->running_friction_torque_magnitude_nm.value,
                    b_output->running_friction_torque_magnitude_nm.value +
                        b_secondary->running_friction_torque_magnitude_nm.value) &&
               b_output->crank_tdc_reference_rad.value ==
                   b_secondary->crank_tdc_reference_rad.value,
           "multiple-crankshaft prescribed fixture decomposition changed aggregate "
           "physical values or phase");

    expect(a_front.bore_m.value == b_front.bore_m.value &&
               a_front.stroke_m.value == b_front.stroke_m.value &&
               a_front.connecting_rod_length_m.value ==
                   b_front.connecting_rod_length_m.value &&
               a_front.journal_phase_rad.value == b_front.journal_phase_rad.value &&
               a_rear.bore_m.value == b_rear.bore_m.value &&
               a_rear.stroke_m.value == b_rear.stroke_m.value &&
               a_rear.connecting_rod_length_m.value ==
                   b_rear.connecting_rod_length_m.value &&
               a_rear.journal_phase_rad.value == b_rear.journal_phase_rad.value,
           "multiple-crankshaft prescribed fixture decomposition changed cylinder "
           "geometry or journal phase");
}

void verify_explicit_output_selection_changes_identity(const FixtureVariant &b) {
    auto secondary_output = b.engine_document;
    secondary_output.engine.output_crankshaft.value = "crank.secondary";
    auto compiled =
        compile_variant(secondary_output, b.scenario_document, b.engine_path);
    const auto selected_inputs = inputs(compiled);
    const auto expected_output =
        crank_id(selected_inputs.engine.engine, "crank.secondary");
    expect(selected_inputs.engine.engine.output_crankshaft_id == expected_output &&
               profile(selected_inputs.engine.engine)
                       .core.mechanism.output_crankshaft_id == expected_output,
           "explicit secondary output selection did not reach both contracts");
    expect(request_identity(compiled).sha256 !=
               request_identity(b.compiled_scenario).sha256,
           "changing only explicit output selection did not change canonical "
           "request identity");
}

void verify_unadmitted_modes_remain_closed(const FixtureVariant &b) {
    const auto resolved = inputs(b.compiled_scenario);
    for (const auto &mode : std::vector<contract::ScenarioMode>{
             contract::HeldSpeed{}, contract::LoadTargetHeldCapture{},
             contract::InertialDyno{}}) {
        auto rejected = resolved.scenario.scenario;
        rejected.mode = mode;
        const auto report =
            contract::validate_for_engine(rejected, resolved.engine.engine);
        expect(std::ranges::any_of(report.issues,
                                   [](const auto &issue) {
                                       return issue.path == "mode" &&
                                              issue.message.find(
                                                  "multiple-crankshaft") !=
                                                  std::string::npos;
                                   }),
               "multiple-crankshaft engine admitted HeldSpeed, LoadTargetHeld, or "
               "InertialDyno motion");
    }
}

[[nodiscard]] authoring::CrankshaftDefinition &
secondary_authored_crank(authoring::EnginePackageDocument &document) {
    const auto found = std::ranges::find(
        document.engine.crankshafts, std::string_view{"crank.secondary"},
        [](const auto &crankshaft) -> std::string_view { return crankshaft.id.value; });
    if (found == document.engine.crankshafts.end()) {
        throw std::runtime_error{
            "multiple-crankshaft dynamic fixture omitted its secondary crank"};
    }
    return *found;
}

[[nodiscard]] double
resolved_crank_friction_magnitude_nm(const compile::CompiledScenario &scenario) {
    const auto resolved = inputs(scenario);
    const auto &mechanism = profile(resolved.engine.engine).core.mechanism;
    double total = 0.0;
    for (const auto &crank : mechanism.cranks) {
        total += crank.running_friction_torque_magnitude_nm.value;
    }
    return total;
}

[[nodiscard]] std::uint64_t
dynamic_release_frame(const compile::CompiledScenario &scenario) {
    const auto resolved = inputs(scenario);
    const auto *preparation = std::get_if<contract::FixedHorizonCycleSampling>(
        &resolved.scenario.scenario.preparation);
    expect(preparation != nullptr,
           "multiple-crankshaft dynamic fixture lost fixed-horizon preparation");
    const auto frame =
        contract::resolve_frame_index(preparation->fixed_preparation_horizon_s.value,
                                      resolved.scenario.scenario.rates.physics);
    expect(frame.has_value() && *frame != 0U,
           "multiple-crankshaft dynamic release is not an exact positive frame");
    return *frame;
}

void verify_dynamic_ab_equivalence(const FixtureVariant &a, const FixtureVariant &b) {
    const auto a_identity = request_identity(a.compiled_scenario);
    const auto b_identity = request_identity(b.compiled_scenario);
    expect(a_identity.sha256 != b_identity.sha256 &&
               a_identity.bytes != b_identity.bytes,
           "one-crank and two-crank dynamic fixtures produced one canonical "
           "request identity");

    const auto a_inputs = inputs(a.compiled_scenario);
    const auto b_inputs = inputs(b.compiled_scenario);
    const auto &a_mode =
        std::get<contract::FreeEngine>(a_inputs.scenario.scenario.mode);
    const auto &b_mode =
        std::get<contract::FreeEngine>(b_inputs.scenario.scenario.mode);
    const auto &a_crank_method =
        resolution_record(a.compiled_scenario, "scenario.mode.crank_dynamics_method");
    const auto &b_crank_method =
        resolution_record(b.compiled_scenario, "scenario.mode.crank_dynamics_method");
    const auto &a_inertia_method = resolution_record(
        a.compiled_scenario, "scenario.mode.engine_baseline_inertia_kg_m2");
    const auto &b_inertia_method = resolution_record(
        b.compiled_scenario, "scenario.mode.engine_baseline_inertia_kg_m2");
    expect(
        a_mode.crank_dynamics_method.value ==
                simulation::
                    nonnegative_speed_free_engine_centered_slider_crank_method_identity() &&
            b_mode.crank_dynamics_method.value ==
                simulation::
                    nonnegative_speed_free_engine_centered_slider_crank_rigid_group_method_identity(),
        "one-crank or rigid-group dynamics lost its resolved method identity");
    expect(a_crank_method.method == a_mode.crank_dynamics_method.value &&
               b_crank_method.method == b_mode.crank_dynamics_method.value,
           "one-crank or rigid-group dynamics provenance names the wrong method");
    expect(a_crank_method.dependency_parameter_paths ==
               std::vector<std::string>{"scenario.mode.kind"},
           "one-crank dynamics provenance lost its exact dependency set");
    expect(
        b_crank_method.dependency_parameter_paths ==
            std::vector<std::string>{"engine.crankshafts.crank.output.semantic_id",
                                     "engine.crankshafts.crank.secondary.semantic_id",
                                     "scenario.mode.kind"},
        "rigid-group dynamics provenance lost its exact topology dependencies");
    expect(
        a_inertia_method.method ==
                simulation::
                    centered_slider_crank_cycle_mean_inertia_method_identity() &&
            b_inertia_method.method ==
                simulation::
                    centered_slider_crank_rigid_group_cycle_mean_inertia_method_identity(),
        "one-crank or rigid-group inertia provenance names the wrong method");

    const auto a_capture = capture_dynamic_motion(a.compiled_scenario);
    const auto b_capture = capture_dynamic_motion(b.compiled_scenario);
    expect(same_capture_trace(a_capture.frames, b_capture.frames),
           "aggregate-equivalent crank decomposition changed dynamic telemetry");
    expect(a_capture.completion == b_capture.completion,
           "aggregate-equivalent crank decomposition changed dynamic completion");

    const auto release_frame = dynamic_release_frame(a.compiled_scenario);
    expect(release_frame + 1U < a_capture.frames.size(),
           "multiple-crankshaft dynamic release has no following RPM frame");
    const auto &before_release = a_capture.frames[release_frame - 1U];
    const auto &released = a_capture.frames[release_frame];
    const auto &after_release = a_capture.frames[release_frame + 1U];
    if (!same_f64(before_release.engine_speed_rpm, 3000.0) ||
        !(released.angular_acceleration_rad_s2 < 0.0) ||
        !(after_release.engine_speed_rpm < 3000.0)) {
        throw std::runtime_error{
            "multiple-crankshaft FreeEngine fixture did not release into positive-"
            "speed dynamic coast motion; before-rpm=" +
            std::to_string(before_release.engine_speed_rpm) +
            "; release-alpha=" + std::to_string(released.angular_acceleration_rad_s2) +
            "; release-rpm=" + std::to_string(released.engine_speed_rpm) +
            "; following-rpm=" + std::to_string(after_release.engine_speed_rpm)};
    }
}

void verify_secondary_inertia_changes_dynamic_motion(const FixtureVariant &baseline) {
    auto heavier_document = baseline.engine_document;
    auto &secondary = secondary_authored_crank(heavier_document);
    const double original_inertia = secondary.moment_of_inertia.value;
    secondary.moment_of_inertia.value = original_inertia * 2.0;

    auto normalized = heavier_document;
    secondary_authored_crank(normalized).moment_of_inertia.value = original_inertia;
    expect(normalized == baseline.engine_document,
           "secondary-inertia mutation changed another authored engine field");

    const auto heavier = compile_variant(heavier_document, baseline.scenario_document,
                                         baseline.engine_path);
    const auto baseline_inputs = inputs(baseline.compiled_scenario);
    const auto heavier_inputs = inputs(heavier);
    const auto &baseline_mode =
        std::get<contract::FreeEngine>(baseline_inputs.scenario.scenario.mode);
    const auto &heavier_mode =
        std::get<contract::FreeEngine>(heavier_inputs.scenario.scenario.mode);
    expect(heavier_mode.engine_baseline_inertia_kg_m2.value >
               baseline_mode.engine_baseline_inertia_kg_m2.value,
           "secondary-only inertia mutation did not reach resolved FreeEngine "
           "inertia");

    const auto baseline_capture = capture_dynamic_motion(baseline.compiled_scenario);
    const auto heavier_capture = capture_dynamic_motion(heavier);
    const auto release_frame = dynamic_release_frame(baseline.compiled_scenario);
    const auto &base = baseline_capture.frames.at(release_frame);
    const auto &heavy = heavier_capture.frames.at(release_frame);
    const auto &base_after = baseline_capture.frames.at(release_frame + 1U);
    const auto &heavy_after = heavier_capture.frames.at(release_frame + 1U);
    expect(same_f64(base.torque.instantaneous_net_shaft.value_nm,
                    heavy.torque.instantaneous_net_shaft.value_nm) &&
               base.angular_acceleration_rad_s2 < 0.0 &&
               heavy.angular_acceleration_rad_s2 < 0.0 &&
               heavy.angular_acceleration_rad_s2 > base.angular_acceleration_rad_s2 &&
               heavy_after.engine_speed_rpm > base_after.engine_speed_rpm,
           "increasing only secondary-crank inertia did not reduce coast "
           "deceleration and retain more RPM");
}

void verify_secondary_friction_changes_dynamic_motion(const FixtureVariant &baseline) {
    auto friction_document = baseline.engine_document;
    auto &secondary = secondary_authored_crank(friction_document);
    expect(secondary.friction_torque.has_value(),
           "secondary crank has no authored friction for mutation");
    const double original_friction = secondary.friction_torque->value;
    secondary.friction_torque->value = original_friction * 3.0;

    auto normalized = friction_document;
    secondary_authored_crank(normalized).friction_torque->value = original_friction;
    expect(normalized == baseline.engine_document,
           "secondary-friction mutation changed another authored engine field");

    const auto increased = compile_variant(
        friction_document, baseline.scenario_document, baseline.engine_path);
    const double resolved_friction_delta =
        resolved_crank_friction_magnitude_nm(increased) -
        resolved_crank_friction_magnitude_nm(baseline.compiled_scenario);
    expect(resolved_friction_delta > 0.0,
           "secondary-only friction mutation did not increase resolved crank "
           "friction");

    const auto baseline_capture = capture_dynamic_motion(baseline.compiled_scenario);
    const auto increased_capture = capture_dynamic_motion(increased);
    const auto release_frame = dynamic_release_frame(baseline.compiled_scenario);
    const auto &base = baseline_capture.frames.at(release_frame);
    const auto &more_friction = increased_capture.frames.at(release_frame);
    const auto &base_after = baseline_capture.frames.at(release_frame + 1U);
    const auto &more_friction_after = increased_capture.frames.at(release_frame + 1U);
    const auto actual_friction_delta =
        more_friction.torque.friction_pump_and_accessory.value_nm -
        base.torque.friction_pump_and_accessory.value_nm;
    expect(near(actual_friction_delta, -resolved_friction_delta, 1.0e-12) &&
               same_f64(base.torque.instantaneous_indicated_gas.value_nm,
                        more_friction.torque.instantaneous_indicated_gas.value_nm) &&
               more_friction.torque.instantaneous_net_shaft.value_nm <
                   base.torque.instantaneous_net_shaft.value_nm &&
               more_friction.angular_acceleration_rad_s2 <
                   base.angular_acceleration_rad_s2 &&
               more_friction_after.engine_speed_rpm < base_after.engine_speed_rpm,
           "increasing only secondary-crank friction did not add its exact opposing "
           "torque, increase deceleration, and reduce RPM");
}

void verify_multiple_crank_master_slave_is_rejected(
    const std::filesystem::path &repository_root) {
    const auto engine_path =
        repository_root / kFixtureDirectory / "engine-negative-cross-crank-master.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "multiple-crankshaft master/slave fixture engine parse failed");
    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    const auto result = compile::compile_engine(engine_document, views);
    const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
    expect(report != nullptr &&
               std::ranges::any_of(
                   report->diagnostics,
                   [](const auto &diagnostic) {
                       return diagnostic.code ==
                                  authoring::DiagnosticCode::unsupported_capability &&
                              diagnostic.json_pointer ==
                                  "/engine/physics_profile/mechanism/cranks" &&
                              diagnostic.message.starts_with(
                                  "one-level master-rod execution currently requires "
                                  "one output crankshaft");
                   }),
           "valid authored multiple-crankshaft master/slave mechanism crossed its "
           "execution gate");
}

void run(const std::filesystem::path &repository_root) {
    const auto prescribed_a = load_variant(repository_root, "engine-a-one-crank.json");
    const auto prescribed_b = load_variant(repository_root, "engine-b-two-cranks.json");
    verify_authored_ab_delta(prescribed_a, prescribed_b);
    verify_resolved_ab_invariants(prescribed_a, prescribed_b);
    verify_explicit_output_selection_changes_identity(prescribed_b);
    verify_unadmitted_modes_remain_closed(prescribed_b);
    verify_multiple_crank_master_slave_is_rejected(repository_root);

    verify_exact_session_ab(
        prescribed_a, prescribed_b,
        {crankwave::EngineMotionMode::prescribed_kinematic_sweep,
         ExpectedSessionSidecar::none});

    const auto dynamic_a = load_variant(repository_root, "engine-a-one-crank.json",
                                        "free-engine-coast-3000rpm.json");
    const auto dynamic_b = load_variant(repository_root, "engine-b-two-cranks.json",
                                        "free-engine-coast-3000rpm.json");
    verify_dynamic_ab_equivalence(dynamic_a, dynamic_b);
    verify_exact_session_ab(dynamic_a, dynamic_b,
                            {crankwave::EngineMotionMode::free_engine,
                             ExpectedSessionSidecar::none});
    verify_secondary_inertia_changes_dynamic_motion(dynamic_b);
    verify_secondary_friction_changes_dynamic_motion(dynamic_b);

    const auto held_a = load_variant(repository_root, "engine-a-one-crank.json",
                                     "held-dyno-3000rpm.json");
    const auto held_b = load_variant(repository_root, "engine-b-two-cranks.json",
                                     "held-dyno-3000rpm.json");
    verify_exact_session_ab(held_a, held_b,
                            {crankwave::EngineMotionMode::held_dyno,
                             ExpectedSessionSidecar::held_dyno});

    const auto vehicle_a = load_variant(repository_root, "engine-a-one-crank.json",
                                        "free-vehicle-coast-3000rpm.json");
    const auto vehicle_b = load_variant(repository_root, "engine-b-two-cranks.json",
                                        "free-vehicle-coast-3000rpm.json");
    verify_exact_session_ab(vehicle_a, vehicle_b,
                            {crankwave::EngineMotionMode::free_vehicle,
                             ExpectedSessionSidecar::free_vehicle});
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        run(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "Multiple-crankshaft execution fixture failure: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
