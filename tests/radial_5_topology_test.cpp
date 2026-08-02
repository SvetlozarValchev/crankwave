#include "authored_engine_fixture_support.hpp"

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/session.hpp"
#include "simulation/low_order_capture_session.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile;
namespace compile_detail = engine_sim_offline::compile::detail;
namespace contract = engine_sim_offline::contract;
namespace simulation = engine_sim_offline::simulation;
namespace test = engine_sim_offline::test;

constexpr double kLegacyPi = 3.14159265359;
constexpr double kDegreesToRadians = kLegacyPi / 180.0;
constexpr double kInchesToMetres = 0.0254;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(double left, double right,
                        double tolerance = 1.0e-12) noexcept {
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

[[nodiscard]] std::string diagnostics(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        if (!result.empty()) {
            result += "; ";
        }
        result += issue.path + ": " + issue.message;
    }
    return result.empty() ? "no validation detail" : result;
}

template <class Value, class Report>
[[nodiscard]] Value require(std::variant<Value, Report> result,
                            std::string_view context) {
    if (const auto *report = std::get_if<Report>(&result)) {
        throw std::runtime_error{std::string{context} + ": " + diagnostics(*report)};
    }
    return std::get<Value>(std::move(result));
}

[[nodiscard]] bool has_diagnostic(const authoring::DiagnosticReport &report,
                                  authoring::DiagnosticCode code,
                                  std::string_view path) {
    return std::ranges::any_of(report.diagnostics, [&](const auto &diagnostic) {
        return diagnostic.code == code && diagnostic.json_pointer == path;
    });
}

[[nodiscard]] bool has_validation_issue(const contract::ValidationReport &report,
                                        contract::ContractIssueCode code,
                                        std::string_view path,
                                        std::string_view message_fragment) {
    return std::ranges::any_of(report.issues, [&](const auto &issue) {
        return issue.code == code && issue.path == path &&
               issue.message.find(message_fragment) != std::string::npos;
    });
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

struct RadialSource {
    authoring::EnginePackageDocument engine_document;
    authoring::ScenarioDocument scenario_document;
    compile_detail::ResolvedEnginePackage resolved;
};

[[nodiscard]] RadialSource load_source(const std::filesystem::path &repository_root) {
    const auto engine_path =
        repository_root / "data/engines/radial-5-cleanroom/engine.json";
    const auto scenario_path = repository_root /
                               "data/engines/radial-5-cleanroom/scenarios/"
                               "prescribed-1500rpm.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "radial-5 authored engine parse failed");
    auto scenario_document =
        require(authoring::parse_scenario_document(read_text(scenario_path)),
                "radial-5 authored scenario parse failed");
    const auto references =
        authoring::validate_scenario_references(scenario_document, engine_document);
    if (!references.ok()) {
        throw std::runtime_error{"radial-5 cross-document validation failed: " +
                                 diagnostics(references)};
    }

    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    auto resolved =
        require(compile_detail::resolve_engine_package(engine_document, views),
                "radial-5 engine resolution failed");
    return {std::move(engine_document), std::move(scenario_document),
            std::move(resolved)};
}

struct ExpectedCylinder {
    std::string_view cylinder;
    std::string_view bank;
    std::string_view journal;
    std::string_view wire;
    std::string_view exhaust;
    double bank_degrees;
    double journal_degrees;
    double firing_degrees;
    double intake_centerline_degrees;
    double exhaust_centerline_degrees;
};

constexpr std::array kExpectedCylinders{
    ExpectedCylinder{"cylinder-0", "bank-0", "rj0", "wire-1", "exhaust-0", 0.0, 0.0,
                     0.0, 474.0, 246.0},
    ExpectedCylinder{"cylinder-1", "bank-1", "sj1", "wire-5", "exhaust-0", 72.0, 72.0,
                     288.0, 762.0, 534.0},
    ExpectedCylinder{"cylinder-2", "bank-2", "sj2", "wire-4", "exhaust-1", 144.0, 144.0,
                     576.0, 1050.0, 822.0},
    ExpectedCylinder{"cylinder-3", "bank-3", "sj3", "wire-3", "exhaust-1", 216.0, 216.0,
                     144.0, 618.0, 390.0},
    ExpectedCylinder{"cylinder-4", "bank-4", "sj4", "wire-2", "exhaust-1", 288.0, 288.0,
                     432.0, 906.0, 678.0},
};

[[nodiscard]] const authoring::CamLobeDefinition &
find_lobe(const authoring::EngineDefinition &engine, std::string_view cylinder,
          authoring::PortKind kind) {
    const auto found = std::ranges::find_if(engine.cam_lobes, [&](const auto &lobe) {
        return lobe.cylinder.value == cylinder && lobe.port_kind == kind;
    });
    if (found == engine.cam_lobes.end()) {
        throw std::runtime_error{"missing authored radial-5 cam lobe"};
    }
    return *found;
}

[[nodiscard]] const authoring::CurveDefinition &
find_curve(const authoring::EngineDefinition &engine, std::string_view id) {
    const auto found = std::ranges::find(engine.curves, id, [](const auto &curve) {
        return std::string_view{curve.id.value};
    });
    if (found == engine.curves.end()) {
        throw std::runtime_error{"missing authored radial-5 curve"};
    }
    return *found;
}

[[nodiscard]] const contract::BankSpec &find_bank(const contract::EngineSpec &engine,
                                                  std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.banks, semantic_id, [](const auto &bank) {
            return std::string_view{bank.semantic_id.value};
        });
    if (found == engine.banks.end()) {
        throw std::runtime_error{"missing resolved radial-5 bank"};
    }
    return *found;
}

[[nodiscard]] const contract::CylinderSpec &
find_cylinder(const contract::EngineSpec &engine, std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.cylinders, semantic_id, [](const auto &cylinder) {
            return std::string_view{cylinder.semantic_id.value};
        });
    if (found == engine.cylinders.end()) {
        throw std::runtime_error{"missing resolved radial-5 cylinder"};
    }
    return *found;
}

[[nodiscard]] const contract::RouteSpec &find_route(const contract::EngineSpec &engine,
                                                    std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.routes, semantic_id, [](const auto &route) {
            return std::string_view{route.semantic_id.value};
        });
    if (found == engine.routes.end()) {
        throw std::runtime_error{"missing resolved radial-5 source route"};
    }
    return *found;
}

[[nodiscard]] const contract::LegacyCylinderAssembly &
find_core_cylinder(const contract::LowOrderEngineCoreV1 &core,
                   contract::CylinderId id) {
    const auto found =
        std::ranges::find(core.mechanism.cylinders, id, [](const auto &cylinder) {
            return cylinder.topology.cylinder_id;
        });
    if (found == core.mechanism.cylinders.end()) {
        throw std::runtime_error{"resolved radial-5 core omitted a cylinder"};
    }
    return *found;
}

void verify_authored_associations(const authoring::EnginePackageDocument &package) {
    const auto &engine = package.engine;
    expect(engine.banks.size() == kExpectedCylinders.size() &&
               engine.journals.size() == kExpectedCylinders.size() &&
               engine.cylinders.size() == kExpectedCylinders.size() &&
               engine.cam_lobes.size() == 2U * kExpectedCylinders.size(),
           "radial-5 authored topology cardinality changed");
    expect(package.presentation.cylinder_routes.size() == kExpectedCylinders.size(),
           "radial-5 authored cylinder-route cardinality changed");

    for (std::size_t index = 0; index < kExpectedCylinders.size(); ++index) {
        const auto &expected = kExpectedCylinders[index];
        const auto &bank = engine.banks[index];
        const auto &journal = engine.journals[index];
        const auto &cylinder = engine.cylinders[index];
        const auto &presentation = package.presentation.cylinder_routes[index];
        expect(bank.id.value == expected.bank &&
                   near(bank.angle.value, expected.bank_degrees) &&
                   journal.id.value == expected.journal &&
                   near(journal.phase.value, expected.journal_degrees) &&
                   cylinder.id.value == expected.cylinder &&
                   cylinder.bank.value == expected.bank &&
                   cylinder.journal.value == expected.journal &&
                   cylinder.ignition_wire.value == expected.wire &&
                   cylinder.exhaust.value == expected.exhaust &&
                   presentation.cylinder.value == expected.cylinder &&
                   presentation.route.value == expected.exhaust &&
                   presentation.gain_linear == 1.0,
               "radial-5 authored cylinder/bank/journal/wire/exhaust association "
               "changed");

        if (index == 0U) {
            expect(std::holds_alternative<authoring::CrankshaftJournalAttachment>(
                       journal.attachment),
                   "radial-5 root journal stopped being crankshaft-direct");
        } else {
            const auto *attachment =
                std::get_if<authoring::MasterRodJournalAttachment>(&journal.attachment);
            expect(attachment != nullptr &&
                       attachment->master_cylinder.value == "cylinder-0" &&
                       near(attachment->throw_radius.value, 2.9),
                   "radial-5 slave journal lost its root or 2.9-inch pin throw");
        }

        const auto &intake =
            find_lobe(engine, expected.cylinder, authoring::PortKind::intake);
        const auto &exhaust =
            find_lobe(engine, expected.cylinder, authoring::PortKind::exhaust);
        expect(near(intake.centerline.value, expected.intake_centerline_degrees) &&
                   near(exhaust.centerline.value, expected.exhaust_centerline_degrees),
               "radial-5 authored cylinder-to-cam association changed");
    }

    constexpr std::array<std::string_view, 5U> expected_wires{
        "wire-1", "wire-3", "wire-5", "wire-2", "wire-4"};
    constexpr std::array<double, 5U> expected_angles{0.0, 144.0, 288.0, 432.0, 576.0};
    expect(engine.ignition.firing_order.size() == expected_wires.size(),
           "radial-5 authored firing-order cardinality changed");
    for (std::size_t index = 0; index < expected_wires.size(); ++index) {
        expect(engine.ignition.firing_order[index].wire.value ==
                       expected_wires[index] &&
                   near(engine.ignition.firing_order[index].crank_angle.value,
                        expected_angles[index]),
               "radial-5 authored firing order changed");
    }

    expect(package.presentation.routes.size() == 2U &&
               package.presentation.routes[0].route.value == "exhaust-0" &&
               package.presentation.routes[1].route.value == "exhaust-1" &&
               package.presentation.routes[0].source_gain_linear == 1.0 &&
               package.presentation.routes[1].source_gain_linear == 0.2,
           "radial-5 differentiated exhaust-route presentation changed");

    const auto &intake_flow = find_curve(engine, "intake-valve-flow");
    const auto &exhaust_flow = find_curve(engine, "exhaust-valve-flow");
    expect(intake_flow.samples.size() == 15U && exhaust_flow.samples.size() == 15U &&
               intake_flow.samples[1].output.value == 50.0 &&
               intake_flow.samples.back().output.value == 500.0 &&
               exhaust_flow.samples[1].output.value == 50.0 &&
               exhaust_flow.samples.back().output.value == 420.0,
           "radial-5 resolved two-times head-flow attenuation changed");
}

void verify_resolved_topology(const compile_detail::ResolvedEnginePackage &package) {
    const auto &engine = package.engine;
    expect(engine.cylinder_layout.value == contract::CylinderLayoutKind::other &&
               engine.banks.size() == kExpectedCylinders.size() &&
               engine.cylinders.size() == kExpectedCylinders.size() &&
               engine.routes.size() == 2U,
           "radial-5 did not resolve as a five-bank custom engine with two routes");

    const auto &core = test::low_order_core(engine);
    expect(core.mechanism.cylinders.size() == kExpectedCylinders.size(),
           "radial-5 executable core cylinder cardinality changed");
    for (std::size_t index = 0; index < kExpectedCylinders.size(); ++index) {
        const auto &expected = kExpectedCylinders[index];
        const auto &bank = find_bank(engine, expected.bank);
        const auto &cylinder = find_cylinder(engine, expected.cylinder);
        const auto &assembly = find_core_cylinder(core, cylinder.id);
        const auto &route = find_route(engine, expected.exhaust);
        expect(bank.angle_rad.has_value() &&
                   near(bank.angle_rad->value,
                        expected.bank_degrees * kDegreesToRadians) &&
                   cylinder.bank_id == bank.id &&
                   near(cylinder.journal_phase_rad.value,
                        expected.journal_degrees * kDegreesToRadians) &&
                   near(cylinder.firing_tdc_offset_rad.value,
                        expected.firing_degrees * kDegreesToRadians) &&
                   assembly.topology.exhaust_route_id == route.id &&
                   near(assembly.parameters.ignition_wire_angle_rad.value,
                        expected.firing_degrees * kDegreesToRadians),
               "radial-5 resolved bank/journal/firing/route association changed");

        if (index == 0U) {
            expect(!cylinder.master_rod_attachment.has_value() &&
                       std::holds_alternative<contract::LegacyDirectJournalKinematics>(
                           assembly.kinematics),
                   "radial-5 root stopped resolving as a direct journal");
        } else {
            const auto *kinematics =
                std::get_if<contract::LegacyMasterRodJournalKinematics>(
                    &assembly.kinematics);
            expect(cylinder.master_rod_attachment.has_value() &&
                       cylinder.master_rod_attachment->master_cylinder_id ==
                           engine.cylinders.front().id &&
                       near(cylinder.master_rod_attachment->throw_radius_m.value,
                            2.9 * kInchesToMetres) &&
                       kinematics != nullptr &&
                       kinematics->master_cylinder_id == engine.cylinders.front().id &&
                       near(kinematics->master_local_phase_rad.value,
                            expected.journal_degrees * kDegreesToRadians),
                   "radial-5 slave lost its resolved one-level master attachment");
        }
    }

    const auto &torque = engine.torque_capability.value;
    expect(torque.instantaneous_net_shaft.availability ==
                   contract::Availability::unavailable &&
               torque.cycle_mean_net_shaft.availability ==
                   contract::Availability::unavailable &&
               !torque.equivalent_inertia_available,
           "geometry-only radial-5 falsely advertised torque or inertia");

    const auto plan_result =
        simulation::compile_mechanism_kinematics_plan(engine, core);
    const auto *shared =
        std::get_if<simulation::SharedMechanismKinematicsPlan>(&plan_result);
    expect(shared != nullptr, "radial-5 failed certified mechanism-plan compilation");
    const auto *plan =
        shared == nullptr
            ? nullptr
            : simulation::one_level_master_rod_mechanism_kinematics_plan(*shared);
    expect(plan != nullptr &&
               plan->output_crankshaft_id == core.mechanism.output_crankshaft_id &&
               plan->rigid_crank_group.crankshaft_count == 1U &&
               near(plan->rigid_crank_group.authored_crank_inertia_kg_m2,
                    core.mechanism.cranks.front()
                        .authored_crank_inertia_kg_m2.value) &&
               plan->cylinders.size() == kExpectedCylinders.size() &&
               simulation::direct_mechanism_kinematics_plan(*shared) == nullptr,
           "radial-5 did not select the one-level master-rod plan");

    const auto angular_speed = 1500.0 * 2.0 * kLegacyPi / 60.0;
    for (std::size_t index = 0; index < kExpectedCylinders.size(); ++index) {
        const auto &expected = kExpectedCylinders[index];
        const auto &planned = plan->cylinders[index];
        expect(planned.crankshaft_id ==
                       core.mechanism.cylinders[index].topology.crankshaft_id &&
                   planned.bank_id == engine.cylinders[index].bank_id &&
                   near(planned.piston_mass_kg,
                        core.mechanism.cylinders[index].parameters.piston_mass_kg.value) &&
                   near(planned.connecting_rod_mass_kg,
                        core.mechanism.cylinders[index]
                            .parameters.connecting_rod_mass_kg.value) &&
                   near(planned.connecting_rod_inertia_kg_m2,
                        core.mechanism.cylinders[index]
                            .parameters.connecting_rod_inertia_kg_m2.value) &&
                   near(planned.connecting_rod_center_of_mass_from_big_end_m,
                        core.mechanism.cylinders[index]
                            .parameters
                            .connecting_rod_center_of_mass_from_crank_pin_m.value) &&
                   near(planned.ignition_wire_angle_rad,
                        expected.firing_degrees * kDegreesToRadians),
               "radial-5 plan lost stable bank or ignition binding");
        const simulation::OneLevelMasterRodDirectRootPlan *root = nullptr;
        const simulation::OneLevelMasterRodSlaveAttachmentPlan *slave = nullptr;
        if (index == 0U) {
            root = std::get_if<simulation::OneLevelMasterRodDirectRootPlan>(
                &planned.kinematics);
            expect(root != nullptr &&
                       near(root->driver.crank_radius_m, 2.75 * kInchesToMetres) &&
                       near(root->driver.master_connecting_rod_length_m,
                            12.0 * kInchesToMetres),
                   "radial-5 plan lost its direct master geometry");
        } else {
            slave = std::get_if<simulation::OneLevelMasterRodSlaveAttachmentPlan>(
                &planned.kinematics);
            const auto *pin = slave == nullptr
                                  ? nullptr
                                  : std::get_if<simulation::OneLevelMasterRodSlavePin>(
                                        &slave->cylinder.journal);
            expect(slave != nullptr && slave->master_cylinder_index == 0U &&
                       pin != nullptr &&
                       near(pin->throw_radius_m, 2.9 * kInchesToMetres) &&
                       near(pin->local_phase_rad,
                            expected.journal_degrees * kDegreesToRadians) &&
                       near(slave->cylinder.connecting_rod_length_m,
                            9.1 * kInchesToMetres),
                   "radial-5 plan lost a slave pin or stable master index");
        }

        const auto &root_plan = std::get<simulation::OneLevelMasterRodDirectRootPlan>(
            plan->cylinders.front().kinematics);
        const auto &cylinder_plan = root != nullptr ? root->cylinder : slave->cylinder;
        const auto certificate = simulation::certify_one_level_master_rod_full_cycle(
            root_plan.driver, cylinder_plan);
        const auto sample = simulation::evaluate_one_level_master_rod_plan(
            *plan, index, 1.17809724509625, angular_speed);
        expect(certificate.admitted() && sample.valid &&
                   std::isfinite(sample.piston_axis_position_m) &&
                   std::isfinite(sample.piston_axis_derivative_m_per_rad) &&
                   std::isfinite(sample.chamber_volume_m3) &&
                   sample.chamber_volume_m3 > 0.0 &&
                   std::isfinite(sample.dvolume_dtheta_m3_per_rad) &&
                   std::isfinite(sample.piston_speed_abs_m_s),
               "radial-5 certified plan did not produce finite admitted geometry");
    }
    expect(shared != nullptr && simulation::mechanism_kinematics_plan_matches_source(
                                    *shared, engine, core),
           "radial-5 certified mechanism plan did not remain source-bound");
}

[[nodiscard]] compile_detail::ScenarioResolverContext
scenario_context(const compile_detail::ResolvedEnginePackage &package) {
    return {
        package.engine,
        package.rig.has_value() ? &*package.rig : nullptr,
        package.presentation,
        package.randomness,
        package.provenance,
        package.fuels,
        package.audio_buses,
        {},
        contract::DistributionIntent::local_evaluation,
        {},
    };
}

void verify_scenario_resolution_and_mode_gate(const RadialSource &source) {
    const auto context = scenario_context(source.resolved);
    const auto resolved = require(
        compile_detail::resolve_scenario_document(source.scenario_document, context),
        "radial-5 prescribed scenario resolution failed");
    const auto *prescribed =
        std::get_if<contract::PrescribedKinematicSweep>(&resolved.scenario.mode);
    const auto *settling =
        std::get_if<contract::FixedSettling>(&resolved.scenario.preparation);
    const auto *lane = prescribed == nullptr
                           ? nullptr
                           : std::get_if<contract::FixedRateRpmTrajectory>(
                                 &prescribed->trajectory.rpm);
    expect(prescribed != nullptr && settling != nullptr && lane != nullptr &&
               settling->warm_up_duration_s.value == 0.0 &&
               settling->settling_duration_s.value == 0.0 &&
               lane->rate == resolved.scenario.rates.physics &&
               lane->first_step_index == 0U && lane->post_step_rpm.size() == 800U &&
               std::ranges::all_of(lane->post_step_rpm,
                                   [](double rpm) { return rpm == 1500.0; }) &&
               resolved.request_input.total_physics_frames == 800U,
           "radial-5 external-speed scenario lost its exact 800-sample lane");
    expect(
        contract::validate_for_engine(resolved.scenario, source.resolved.engine).ok(),
        "radial-5 prescribed scenario failed the resolved engine mode contract");

    auto rejected = source.scenario_document;
    const auto &external = std::get<authoring::ExternalSpeedMode>(rejected.mode);
    rejected.mode = authoring::HeldSpeedMode{
        external.engine_speed.points.front().value,
        external.throttle_01,
    };
    const auto gate = compile_detail::resolve_scenario_document(rejected, context);
    const auto *report = std::get_if<authoring::DiagnosticReport>(&gate);
    expect(report != nullptr &&
               has_diagnostic(*report,
                              authoring::DiagnosticCode::unsupported_capability,
                              "/mode/type"),
           "radial-5 non-external-speed mode gate was removed or lost its path");

    const std::array closed_dynamic_modes{
        authoring::ScenarioMode{authoring::FreeEngineMode{}},
        authoring::ScenarioMode{authoring::HeldDynoMode{}},
        authoring::ScenarioMode{authoring::FreeVehicleMode{}},
        authoring::ScenarioMode{authoring::InertialDynoMode{}},
    };
    for (const auto &mode : closed_dynamic_modes) {
        auto dynamic = source.scenario_document;
        dynamic.mode = mode;
        const auto result = compile_detail::resolve_scenario_document(dynamic, context);
        const auto *dynamic_report = std::get_if<authoring::DiagnosticReport>(&result);
        expect(dynamic_report != nullptr &&
                   has_diagnostic(*dynamic_report,
                                  authoring::DiagnosticCode::unsupported_capability,
                                  "/mode/type"),
               "radial-5 authored dynamic mode escaped the master-rod firewall");
    }
}

[[nodiscard]] contract::Sha256Digest nonzero_request_identity() {
    contract::Sha256Digest identity;
    identity.bytes.back() = 1U;
    return identity;
}

void verify_public_capture(const std::filesystem::path &repository_root) {
    const auto fixture = test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/prescribed-1500rpm.json");
    const auto horizon = contract::resolve_frame_index(
        fixture.scenario.total_duration_s.value, fixture.scenario.rates.physics);
    expect(horizon.has_value() && *horizon == 800U,
           "radial-5 public fixture lost its exact finite horizon");

    const std::array closed_dynamic_modes{
        contract::ScenarioMode{contract::FreeEngine{}},
        contract::ScenarioMode{contract::HeldDyno{}},
        contract::ScenarioMode{contract::FreeVehicle{}},
        contract::ScenarioMode{contract::InertialDyno{}},
    };
    for (const auto &mode : closed_dynamic_modes) {
        auto rejected_scenario = fixture.scenario;
        rejected_scenario.mode = mode;
        const auto contract_report =
            contract::validate_for_engine(rejected_scenario, fixture.engine);
        expect(has_validation_issue(contract_report,
                                    contract::ContractIssueCode::unsupported_value,
                                    "mode", "master-rod engines currently admit only"),
               "radial-5 resolved dynamic mode escaped the engine contract firewall");

        const auto rejected_session = simulation::compile_low_order_capture_session(
            fixture.engine, rejected_scenario,
            test::compile_fixture_random_plan(fixture), nonzero_request_identity(),
            simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
        const auto *session_report =
            std::get_if<contract::ValidationReport>(&rejected_session);
        expect(session_report != nullptr &&
                   has_validation_issue(
                       *session_report, contract::ContractIssueCode::unsupported_value,
                       "mode", "master-rod engines currently admit only"),
               "radial-5 dynamic mode reached public capture-session execution");
    }

    auto result = simulation::compile_low_order_capture_session(
        fixture.engine, fixture.scenario, test::compile_fixture_random_plan(fixture),
        nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        throw std::runtime_error{"radial-5 public runtime admission failed: " +
                                 diagnostics(*report)};
    }
    auto session = std::get<simulation::LowOrderCaptureSession>(std::move(result));

    bool observed_frame = false;
    bool observed_active_source = false;
    std::uint64_t observed_frames = 0U;
    while (!session.completed()) {
        auto published =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                const auto report =
                    contract::validate(block, fixture.engine, fixture.scenario);
                if (!report.ok()) {
                    throw std::runtime_error{
                        "radial-5 capture block failed validation: " +
                        diagnostics(report)};
                }
                expect(block.frame_count() > 0U &&
                           block.layout().cylinders().size() == 5U &&
                           block.layout().routes().size() == 2U,
                       "radial-5 public capture layout changed");
                for (std::size_t frame = 0U; frame < block.frame_count(); ++frame) {
                    const auto *engine = block.engine_sample(frame);
                    expect(engine != nullptr && std::isfinite(engine->theta_rad) &&
                               std::isfinite(engine->angular_speed_rad_s) &&
                               engine->engine_speed_rpm == 1500.0,
                           "radial-5 public capture lost prescribed engine motion");
                    for (std::size_t cylinder = 0U; cylinder < 5U; ++cylinder) {
                        const auto *sample = block.cylinder_sample(frame, cylinder);
                        expect(sample != nullptr &&
                                   std::isfinite(sample->chamber_volume_m3) &&
                                   sample->chamber_volume_m3 > 0.0 &&
                                   std::isfinite(
                                       sample->chamber_dvolume_dtheta_m3_per_rad) &&
                                   std::isfinite(sample->piston_velocity_m_s) &&
                                   std::isfinite(sample->pressure_pa_abs) &&
                                   sample->pressure_pa_abs > 0.0 &&
                                   std::isfinite(sample->temperature_k) &&
                                   sample->temperature_k > 0.0 &&
                                   std::isfinite(sample->amount_mol) &&
                                   sample->amount_mol >= 0.0,
                               "radial-5 public capture emitted a nonfinite chamber "
                               "sample");
                    }
                    for (std::size_t route = 0U; route < 2U; ++route) {
                        const auto *sample =
                            block.gas_source_route_sample(frame, route);
                        expect(sample != nullptr &&
                                   std::isfinite(sample->pressure_pa_abs) &&
                                   sample->pressure_pa_abs > 0.0 &&
                                   std::isfinite(sample->temperature_k) &&
                                   sample->temperature_k > 0.0 &&
                                   std::isfinite(sample->signed_mass_flow_kg_s) &&
                                   std::isfinite(sample->effective_area_m2) &&
                                   sample->effective_area_m2 >= 0.0,
                               "radial-5 public capture emitted a nonfinite audio "
                               "source sample");
                        observed_active_source =
                            observed_active_source ||
                            sample->signed_mass_flow_kg_s != 0.0 ||
                            sample->pressure_pa_abs !=
                                fixture.scenario.ambient.pressure_pa_abs.value;
                    }
                    observed_frame = true;
                }
                observed_frames += block.frame_count();
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&published)) {
            throw std::runtime_error{"radial-5 capture faulted (" +
                                     failure->detail_code +
                                     "): " + failure->state_summary};
        }
    }
    expect(observed_frame && observed_active_source && observed_frames == *horizon &&
               session.published_sample_count() == *horizon && session.completed() &&
               !session.faulted(),
           "radial-5 public finite capture did not complete exactly 800 frames");
}

void verify_public_audio_session(const std::filesystem::path &repository_root,
                                 const RadialSource &source) {
    const auto engine_path =
        repository_root / "data/engines/radial-5-cleanroom/engine.json";
    const auto assets = load_assets(source.engine_document, engine_path);
    const auto views = asset_views(assets);
    auto engine = require(compile::compile_engine(source.engine_document, views),
                          "radial-5 public engine compilation failed");
    const std::array closed_dynamic_modes{
        authoring::ScenarioMode{authoring::FreeEngineMode{}},
        authoring::ScenarioMode{authoring::HeldDynoMode{}},
        authoring::ScenarioMode{authoring::FreeVehicleMode{}},
        authoring::ScenarioMode{authoring::InertialDynoMode{}},
    };
    for (const auto &mode : closed_dynamic_modes) {
        auto rejected = source.scenario_document;
        rejected.mode = mode;
        const auto result = compile::compile_scenario(engine, rejected);
        const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
        expect(report != nullptr &&
                   has_diagnostic(*report,
                                  authoring::DiagnosticCode::unsupported_capability,
                                  "/mode/type"),
               "radial-5 dynamic mode reached the public compiled scenario API");
    }
    auto scenario = require(compile::compile_scenario(engine, source.scenario_document),
                            "radial-5 public scenario compilation failed");
    auto created = engine_sim_offline::create_engine_session(
        scenario, engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&created)) {
        throw std::runtime_error{"radial-5 public session creation failed: " +
                                 error->detail_code + ": " + error->message};
    }
    auto session = std::get<engine_sim_offline::EngineSession>(std::move(created));
    const auto descriptor = session.descriptor();
    expect(descriptor.engine_id == "radial-5-cleanroom" &&
               descriptor.motion_mode ==
                   engine_sim_offline::EngineMotionMode::prescribed_kinematic_sweep &&
               descriptor.total_block_count == 4U &&
               descriptor.preparation_block_count == 0U &&
               descriptor.audio_buses.size() == 8U &&
               descriptor.live_control_capabilities == 0U,
           "radial-5 public session descriptor changed");

    std::uint64_t block_count = 0U;
    bool observed_nonzero_audio = false;
    while (true) {
        auto result = session.process_block();
        if (const auto *block =
                std::get_if<engine_sim_offline::EngineSessionBlockView>(&result)) {
            expect(block->phase() ==
                           engine_sim_offline::EngineSessionBlockPhase::audible &&
                       block->physics_frame_count() == 200U &&
                       block->delivery_frame_count() == 3840U &&
                       block->audio_buses().size() == descriptor.audio_buses.size(),
                   "radial-5 public audio block changed shape");
            for (const auto &bus : block->audio_buses()) {
                expect(bus.samples.size() == 3840U &&
                           std::ranges::all_of(
                               bus.samples,
                               [](float sample) { return std::isfinite(sample); }),
                       "radial-5 public audio bus emitted a nonfinite or short block");
                observed_nonzero_audio =
                    observed_nonzero_audio ||
                    std::ranges::any_of(bus.samples,
                                        [](float sample) { return sample != 0.0F; });
            }
            ++block_count;
            continue;
        }
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
            throw std::runtime_error{"radial-5 public session faulted: " +
                                     error->detail_code + ": " + error->message};
        }
        const auto &completed =
            std::get<engine_sim_offline::EngineSessionCompleted>(result);
        expect(block_count == descriptor.total_block_count &&
                   completed.block_count == block_count &&
                   completed.physics_frame_count == 800U &&
                   completed.delivery_frame_count == 15360U &&
                   !completed.live_controls_accepted && observed_nonzero_audio,
               "radial-5 public audio session did not complete with finite PCM");
        break;
    }
}

void run(const std::filesystem::path &repository_root) {
    const auto source = load_source(repository_root);
    verify_authored_associations(source.engine_document);
    verify_resolved_topology(source.resolved);
    verify_scenario_resolution_and_mode_gate(source);
    verify_public_capture(repository_root);
    verify_public_audio_session(repository_root, source);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        run(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "Radial 5 topology failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
