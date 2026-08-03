#include "authored_engine_fixture_support.hpp"

#include "compile/engine_resolver.hpp"
#include "compile/scenario_resolver.hpp"
#include "engine_sim_offline/authoring/parse.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/session.hpp"
#include "simulation/bounded_dyno_constraint.hpp"
#include "simulation/chen_flynn_per_cylinder_travel_cycle_mean_loss.hpp"
#include "simulation/free_engine_method_registry.hpp"
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
    authoring::ScenarioDocument free_engine_scenario_document;
    authoring::ScenarioDocument held_dyno_scenario_document;
    authoring::ScenarioDocument free_vehicle_control_scenario_document;
    authoring::ScenarioDocument free_vehicle_candidate_scenario_document;
    compile_detail::ResolvedEnginePackage resolved;
};

[[nodiscard]] RadialSource load_source(const std::filesystem::path &repository_root) {
    const auto engine_path =
        repository_root / "data/engines/radial-5-cleanroom/engine.json";
    auto engine_document =
        require(authoring::parse_engine_document(read_text(engine_path)),
                "radial-5 authored engine parse failed");
    const auto scenarios_path = engine_path.parent_path() / "scenarios";
    const auto load_scenario = [&](std::string_view filename, std::string_view label) {
        auto document =
            require(authoring::parse_scenario_document(
                        read_text(scenarios_path / std::string{filename})),
                    std::string{"radial-5 "} + std::string{label} + " parse failed");
        const auto references =
            authoring::validate_scenario_references(document, engine_document);
        if (!references.ok()) {
            throw std::runtime_error{
                std::string{"radial-5 "} + std::string{label} +
                " cross-document validation failed: " + diagnostics(references)};
        }
        return document;
    };
    auto scenario_document = load_scenario("prescribed-1500rpm.json", "prescribed");
    auto free_engine_scenario_document =
        load_scenario("warm-running-free-rev-1500rpm.json", "FreeEngine");
    auto held_dyno_scenario_document =
        load_scenario("held-dyno-pull-hold-lift-1500-2800rpm.json", "HeldDyno");
    auto free_vehicle_control_scenario_document = load_scenario(
        "free-vehicle-propellor-direct-drive-open-clutch-control-1500rpm.json",
        "FreeVehicle control");
    auto free_vehicle_candidate_scenario_document = load_scenario(
        "free-vehicle-propellor-direct-drive-20pct-clutch-candidate-1500rpm.json",
        "FreeVehicle candidate");

    const auto assets = load_assets(engine_document, engine_path);
    const auto views = asset_views(assets);
    auto resolved =
        require(compile_detail::resolve_engine_package(engine_document, views),
                "radial-5 engine resolution failed");
    return {std::move(engine_document),
            std::move(scenario_document),
            std::move(free_engine_scenario_document),
            std::move(held_dyno_scenario_document),
            std::move(free_vehicle_control_scenario_document),
            std::move(free_vehicle_candidate_scenario_document),
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
                   contract::Availability::available &&
               torque.instantaneous_net_shaft.completeness ==
                   contract::Completeness::complete &&
               torque.instantaneous_net_shaft.included_terms ==
                   contract::known_torque_term_mask() &&
               torque.instantaneous_net_shaft.omitted_terms == 0 &&
               torque.cycle_mean_net_shaft.availability ==
                   contract::Availability::available &&
               torque.cycle_mean_net_shaft.completeness ==
                   contract::Completeness::complete &&
               torque.cycle_mean_net_shaft.included_terms ==
                   contract::known_torque_term_mask() &&
               torque.cycle_mean_net_shaft.omitted_terms == 0 &&
               torque.equivalent_inertia_available,
           "certified radial-5 lost complete operating torque or inertia capability");
    expect(
        engine.methods.losses.value ==
            simulation::
                chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_identity(),
        "radial-5 did not select per-cylinder piston-travel Chen-Flynn loss "
        "authority");
    const auto loss_resolution =
        std::ranges::find(package.provenance.resolutions, "engine.methods.losses",
                          &contract::ResolutionRecord::parameter_path);
    const std::vector<std::string> expected_loss_dependencies{
        "engine.cylinders.cylinder-1.master_rod_attachment.throw_radius_m",
        "engine.cylinders.cylinder-2.master_rod_attachment.throw_radius_m",
        "engine.cylinders.cylinder-3.master_rod_attachment.throw_radius_m",
        "engine.cylinders.cylinder-4.master_rod_attachment.throw_radius_m",
        "engine.profile_id",
    };
    expect(loss_resolution != package.provenance.resolutions.end() &&
               loss_resolution->dependency_parameter_paths ==
                   expected_loss_dependencies,
           "radial-5 loss selection lost its explicit master-rod topology "
           "provenance");

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
                    core.mechanism.cranks.front().authored_crank_inertia_kg_m2.value) &&
               plan->cylinders.size() == kExpectedCylinders.size() &&
               simulation::direct_mechanism_kinematics_plan(*shared) == nullptr,
           "radial-5 did not select the one-level master-rod plan");

    const auto angular_speed = 1500.0 * 2.0 * kLegacyPi / 60.0;
    for (std::size_t index = 0; index < kExpectedCylinders.size(); ++index) {
        const auto &expected = kExpectedCylinders[index];
        const auto &planned = plan->cylinders[index];
        expect(
            planned.crankshaft_id ==
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
                         .parameters.connecting_rod_center_of_mass_from_crank_pin_m
                         .value) &&
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
               lane->first_step_index == 0U && lane->post_step_rpm.size() == 1600U &&
               std::ranges::all_of(lane->post_step_rpm,
                                   [](double rpm) { return rpm == 1500.0; }) &&
               resolved.request_input.total_physics_frames == 1600U,
           "radial-5 external-speed scenario lost its exact 1600-sample lane");
    expect(
        contract::validate_for_engine(resolved.scenario, source.resolved.engine).ok(),
        "radial-5 prescribed scenario failed the resolved engine mode contract");

    const auto free_resolved =
        require(compile_detail::resolve_scenario_document(
                    source.free_engine_scenario_document, context),
                "radial-5 FreeEngine scenario resolution failed");
    const auto *free_engine =
        std::get_if<contract::FreeEngine>(&free_resolved.scenario.mode);
    const auto *sampling = std::get_if<contract::FixedHorizonCycleSampling>(
        &free_resolved.scenario.preparation);
    const auto &core = test::low_order_core(source.resolved.engine);
    const auto plan_result =
        simulation::compile_mechanism_kinematics_plan(source.resolved.engine, core);
    const auto *shared =
        std::get_if<simulation::SharedMechanismKinematicsPlan>(&plan_result);
    const auto *radial =
        shared == nullptr
            ? nullptr
            : simulation::one_level_master_rod_mechanism_kinematics_plan(*shared);
    expect(
        free_engine != nullptr && sampling != nullptr && radial != nullptr &&
            free_engine->crank_dynamics_method.value ==
                simulation::
                    nonnegative_speed_free_engine_one_level_master_rod_method_identity() &&
            free_engine->engine_baseline_inertia_kg_m2.value ==
                radial->cycle_mean_inertia.engine_equivalent_inertia_kg_m2 &&
            free_engine->initial_engine_speed_rpm.value == 1500.0 &&
            sampling->fixed_preparation_horizon_s.value == 0.5 &&
            free_resolved.request_input.total_physics_frames == 104000U &&
            contract::validate_for_engine(free_resolved.scenario,
                                          source.resolved.engine)
                .ok(),
        "radial-5 FreeEngine scenario lost its articulated method, inertia, or "
        "warm preparation authority");
    const auto crank_method_resolution =
        std::ranges::find(free_resolved.combined_provenance.resolutions,
                          "scenario.mode.crank_dynamics_method",
                          &contract::ResolutionRecord::parameter_path);
    const std::vector<std::string> expected_topology_method_dependencies{
        "engine.cylinders.cylinder-1.master_rod_attachment.throw_radius_m",
        "engine.cylinders.cylinder-2.master_rod_attachment.throw_radius_m",
        "engine.cylinders.cylinder-3.master_rod_attachment.throw_radius_m",
        "engine.cylinders.cylinder-4.master_rod_attachment.throw_radius_m",
        "scenario.mode.kind",
    };
    expect(crank_method_resolution !=
                   free_resolved.combined_provenance.resolutions.end() &&
               crank_method_resolution->dependency_parameter_paths ==
                   expected_topology_method_dependencies,
           "radial-5 FreeEngine method selection lost its explicit master-rod "
           "topology provenance");

    const auto *authored_rig =
        source.engine_document.rig ? &*source.engine_document.rig : nullptr;
    const auto *authored_vehicle = authored_rig != nullptr && authored_rig->vehicle
                                       ? &*authored_rig->vehicle
                                       : nullptr;
    const auto *authored_transmission =
        authored_rig != nullptr && authored_rig->transmission
            ? &*authored_rig->transmission
            : nullptr;
    expect(authored_rig != nullptr && authored_vehicle != nullptr &&
               authored_transmission != nullptr &&
               authored_rig->id.value == "radial-5-propellor-direct-drive-evaluation" &&
               authored_vehicle->id.value == "radial-5-propellor-proxy" &&
               authored_vehicle->mass.value == 100.0 &&
               authored_vehicle->mass.unit == "lb" &&
               authored_vehicle->drag_coefficient == 0.5 &&
               authored_vehicle->frontal_area.value == 705.0 &&
               authored_vehicle->frontal_area.unit == "in2" &&
               authored_vehicle->differential_ratio == 1.0 &&
               authored_vehicle->tire_radius.value == 1.0 &&
               authored_vehicle->tire_radius.unit == "m" &&
               authored_vehicle->rolling_resistance_force.value == 300.0 &&
               authored_vehicle->rolling_resistance_force.unit == "N" &&
               !authored_vehicle->maximum_service_brake_force.has_value() &&
               authored_transmission->id.value == "radial-5-direct-drive" &&
               authored_transmission->maximum_clutch_torque.value == 500.0 &&
               authored_transmission->maximum_clutch_torque.unit == "lb*ft" &&
               authored_transmission->gears.size() == 1U &&
               authored_transmission->gears.front().id.value == "gear-1" &&
               authored_transmission->gears.front().ratio == 1.0,
           "radial-5 authored propellor/direct-drive source rig changed");

    const auto control_resolved =
        require(compile_detail::resolve_scenario_document(
                    source.free_vehicle_control_scenario_document, context),
                "radial-5 FreeVehicle control scenario resolution failed");
    const auto candidate_resolved =
        require(compile_detail::resolve_scenario_document(
                    source.free_vehicle_candidate_scenario_document, context),
                "radial-5 FreeVehicle candidate scenario resolution failed");
    const auto *control_vehicle =
        std::get_if<contract::FreeVehicle>(&control_resolved.scenario.mode);
    const auto *candidate_vehicle =
        std::get_if<contract::FreeVehicle>(&candidate_resolved.scenario.mode);
    const auto *candidate_sampling = std::get_if<contract::FixedHorizonCycleSampling>(
        &candidate_resolved.scenario.preparation);
    expect(
        control_vehicle != nullptr && candidate_vehicle != nullptr &&
            candidate_sampling != nullptr &&
            candidate_vehicle->crank_dynamics_method.value ==
                simulation::
                    nonnegative_speed_free_engine_one_level_master_rod_method_identity() &&
            candidate_vehicle->engine_baseline_inertia_kg_m2.value ==
                free_engine->engine_baseline_inertia_kg_m2.value &&
            candidate_vehicle->initial_engine_speed_rpm.value == 1500.0 &&
            candidate_vehicle->initial_theta_rad.value == 1.17809724509625 &&
            candidate_vehicle->initial_vehicle_speed_m_s.value == 157.07963267948966 &&
            candidate_sampling->fixed_preparation_horizon_s.value == 0.5 &&
            candidate_resolved.request_input.total_physics_frames == 104000U &&
            control_resolved.request_input.total_physics_frames == 104000U &&
            contract::validate_for_engine(candidate_resolved.scenario,
                                          source.resolved.engine)
                .ok() &&
            contract::validate_for_engine(control_resolved.scenario,
                                          source.resolved.engine)
                .ok(),
        "radial-5 FreeVehicle scenarios lost their articulated method, exact "
        "frame grid, or initial conditions");
    expect(candidate_vehicle->rig.semantic_id.value ==
                   "radial-5-propellor-direct-drive-evaluation" &&
               candidate_vehicle->rig.transmission.gears.size() == 1U &&
               candidate_vehicle->rig.transmission.gears.front().semantic_id.value ==
                   "gear-1" &&
               candidate_vehicle->rig.transmission.gears.front().ratio.value == 1.0 &&
               candidate_vehicle->selected_gear.value.size() == 1U &&
               candidate_vehicle->selected_gear.value.front().gear_id ==
                   candidate_vehicle->rig.transmission.gears.front().id &&
               candidate_vehicle->clutch_engagement_01.value.size() == 3U &&
               candidate_vehicle->clutch_engagement_01.value[1].time_s == 0.65 &&
               candidate_vehicle->clutch_engagement_01.value[1].value == 0.2 &&
               candidate_vehicle->clutch_engagement_01.value[2].time_s == 2.4 &&
               candidate_vehicle->clutch_engagement_01.value[2].value == 0.0 &&
               control_vehicle->clutch_engagement_01.value.size() == 1U &&
               control_vehicle->clutch_engagement_01.value.front().value == 0.0,
           "radial-5 FreeVehicle source rig or authored drivetrain lanes changed "
           "during resolution");

    const auto find_resolution = [](const auto &contracts,
                                    std::string_view parameter_path) {
        return std::ranges::find(contracts.combined_provenance.resolutions,
                                 parameter_path,
                                 &contract::ResolutionRecord::parameter_path);
    };
    const auto free_inertia_resolution =
        find_resolution(free_resolved, "scenario.mode.engine_baseline_inertia_kg_m2");
    const auto candidate_inertia_resolution = find_resolution(
        candidate_resolved, "scenario.mode.engine_baseline_inertia_kg_m2");
    const auto candidate_crank_method_resolution =
        find_resolution(candidate_resolved, "scenario.mode.crank_dynamics_method");
    expect(free_inertia_resolution !=
                   free_resolved.combined_provenance.resolutions.end() &&
               candidate_inertia_resolution !=
                   candidate_resolved.combined_provenance.resolutions.end() &&
               candidate_crank_method_resolution !=
                   candidate_resolved.combined_provenance.resolutions.end() &&
               candidate_inertia_resolution->method ==
                   free_inertia_resolution->method &&
               candidate_inertia_resolution->dependency_parameter_paths ==
                   free_inertia_resolution->dependency_parameter_paths &&
               candidate_crank_method_resolution->method ==
                   crank_method_resolution->method &&
               candidate_crank_method_resolution->dependency_parameter_paths ==
                   crank_method_resolution->dependency_parameter_paths,
           "radial-5 FreeVehicle did not retain the accepted FreeEngine topology "
           "and baseline-inertia provenance");

    const auto held_dyno_resolved =
        require(compile_detail::resolve_scenario_document(
                    source.held_dyno_scenario_document, context),
                "radial-5 HeldDyno scenario resolution failed");
    const auto *held_dyno =
        std::get_if<contract::HeldDyno>(&held_dyno_resolved.scenario.mode);
    const auto *held_dyno_sampling = std::get_if<contract::FixedHorizonCycleSampling>(
        &held_dyno_resolved.scenario.preparation);
    expect(
        held_dyno != nullptr && held_dyno_sampling != nullptr &&
            held_dyno->constraint_method.value ==
                simulation::
                    bounded_held_dyno_one_level_master_rod_constraint_method_identity() &&
            held_dyno->initial_engine_speed_rpm.value == 1500.0 &&
            held_dyno_sampling->fixed_preparation_horizon_s.value == 0.5 &&
            held_dyno->target_engine_speed_rpm.post_step_rpm.size() == 120000U &&
            held_dyno_resolved.request_input.total_physics_frames == 120000U &&
            contract::validate_for_engine(held_dyno_resolved.scenario,
                                          source.resolved.engine)
                .ok(),
        "radial-5 HeldDyno scenario lost its articulated constraint, exact frame "
        "grid, or warm preparation authority");
    const auto held_dyno_method_resolution = std::ranges::find(
        held_dyno_resolved.combined_provenance.resolutions,
        "scenario.mode.constraint_method", &contract::ResolutionRecord::parameter_path);
    expect(held_dyno_method_resolution !=
                   held_dyno_resolved.combined_provenance.resolutions.end() &&
               held_dyno_method_resolution->dependency_parameter_paths ==
                   expected_topology_method_dependencies,
           "radial-5 HeldDyno method selection lost its exact master-rod topology "
           "provenance");

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
               "radial-5 unsupported dynamic mode escaped the master-rod "
               "firewall");
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
    expect(horizon.has_value() && *horizon == 1600U,
           "radial-5 public fixture lost its exact finite horizon");

    const std::array closed_dynamic_modes{
        contract::ScenarioMode{contract::InertialDyno{}},
    };
    for (const auto &mode : closed_dynamic_modes) {
        auto rejected_scenario = fixture.scenario;
        rejected_scenario.mode = mode;
        const auto contract_report =
            contract::validate_for_engine(rejected_scenario, fixture.engine);
        if (!has_validation_issue(contract_report,
                                  contract::ContractIssueCode::unsupported_value,
                                  "mode", "master-rod engines currently admit only")) {
            throw std::runtime_error{
                "radial-5 resolved dynamic mode escaped the engine contract "
                "firewall: " +
                diagnostics(contract_report)};
        }

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
           "radial-5 public finite capture did not complete exactly 1600 frames");

    const auto free_fixture = test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/"
        "warm-running-free-rev-1500rpm.json");
    const auto free_horizon =
        contract::resolve_frame_index(free_fixture.scenario.total_duration_s.value,
                                      free_fixture.scenario.rates.physics);
    expect(free_horizon.has_value() && *free_horizon == 104000U,
           "radial-5 FreeEngine fixture lost its exact finite horizon");
    auto free_result = simulation::compile_low_order_capture_session(
        free_fixture.engine, free_fixture.scenario,
        test::compile_fixture_random_plan(free_fixture), nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*free_horizon));
    if (const auto *report = std::get_if<contract::ValidationReport>(&free_result)) {
        throw std::runtime_error{
            "radial-5 public FreeEngine runtime admission failed: " +
            diagnostics(*report)};
    }
    auto free_session =
        std::get<simulation::LowOrderCaptureSession>(std::move(free_result));
    std::uint64_t free_frames = 0U;
    bool observed_released_motion = false;
    while (free_frames < 20000U) {
        auto published = free_session.publish_next_block(
            [&](const contract::CaptureBlockView &block) {
                for (std::size_t frame = 0U; frame < block.frame_count(); ++frame) {
                    const auto *engine = block.engine_sample(frame);
                    expect(engine != nullptr &&
                               std::isfinite(engine->engine_speed_rpm) &&
                               engine->engine_speed_rpm > 0.0,
                           "radial-5 FreeEngine emitted nonpositive engine motion");
                    if (free_frames + frame < 10000U) {
                        expect(engine->engine_speed_rpm == 1500.0,
                               "radial-5 FreeEngine warm preparation lost its exact "
                               "held speed");
                    } else {
                        observed_released_motion = observed_released_motion ||
                                                   engine->engine_speed_rpm != 1500.0;
                    }
                }
                free_frames += block.frame_count();
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&published)) {
            throw std::runtime_error{"radial-5 FreeEngine capture faulted (" +
                                     failure->detail_code +
                                     "): " + failure->state_summary};
        }
    }
    expect(observed_released_motion && free_frames == 20000U &&
               free_session.published_sample_count() == 20000U &&
               !free_session.completed() && !free_session.faulted(),
           "radial-5 public FreeEngine did not hold, release, and advance exactly");
}

void verify_public_free_vehicle_capture(const std::filesystem::path &repository_root) {
    const auto candidate = test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/"
        "free-vehicle-propellor-direct-drive-20pct-clutch-candidate-1500rpm.json");
    const auto control = test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/"
        "free-vehicle-propellor-direct-drive-open-clutch-control-1500rpm.json");
    const auto horizon = contract::resolve_frame_index(
        candidate.scenario.total_duration_s.value, candidate.scenario.rates.physics);
    const auto release = contract::resolve_frame_index(
        candidate.scenario.audible_start_s.value, candidate.scenario.rates.physics);
    expect(horizon.has_value() && *horizon == 104000U && release.has_value() &&
               *release == 10000U,
           "radial-5 public FreeVehicle fixture lost its exact frame grid");

    auto wrong_method_scenario = candidate.scenario;
    std::get<contract::FreeVehicle>(wrong_method_scenario.mode)
        .crank_dynamics_method.value = simulation::
        nonnegative_speed_free_engine_centered_slider_crank_method_identity();
    const auto wrong_method_result = simulation::compile_low_order_capture_session(
        candidate.engine, wrong_method_scenario,
        test::compile_fixture_random_plan(candidate), nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
    const auto *wrong_method_report =
        std::get_if<contract::ValidationReport>(&wrong_method_result);
    expect(wrong_method_report != nullptr &&
               has_validation_issue(
                   *wrong_method_report, contract::ContractIssueCode::unsupported_value,
                   "scenario.mode.crank_dynamics_method.value", "mechanism-family"),
           "radial-5 FreeVehicle admitted the direct centered-slider crank "
           "identity");

    expect(
        std::holds_alternative<simulation::LowOrderCaptureSession>(
            simulation::compile_low_order_capture_session(
                control.engine, control.scenario,
                test::compile_fixture_random_plan(control), nonzero_request_identity(),
                simulation::LowOrderExecutionExtent::finite_scenario(*horizon))),
        "radial-5 FreeVehicle open-clutch control failed admission");
    auto session = require(
        simulation::compile_low_order_capture_session(
            candidate.engine, candidate.scenario,
            test::compile_fixture_random_plan(candidate), nonzero_request_identity(),
            simulation::LowOrderExecutionExtent::finite_scenario(*horizon)),
        "radial-5 FreeVehicle candidate admission failed");

    std::uint64_t observed_frames = 0U;
    bool observed_released_motion = false;
    bool observed_loaded_clutch = false;
    constexpr std::uint64_t kSmokeFrameCount = 14000U;
    while (observed_frames < kSmokeFrameCount) {
        auto published =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                expect(block.clock().first_sample_index == observed_frames,
                       "radial-5 FreeVehicle capture lost contiguous frame order");
                for (std::size_t index = 0U; index < block.frame_count(); ++index) {
                    const auto sample_index = block.clock().first_sample_index + index;
                    const auto *sample = block.engine_sample(index);
                    expect(sample != nullptr &&
                               std::isfinite(sample->engine_speed_rpm) &&
                               sample->engine_speed_rpm > 0.0 &&
                               std::isfinite(sample->theta_rad) &&
                               std::isfinite(sample->angular_speed_rad_s) &&
                               sample->angular_speed_rad_s > 0.0,
                           "radial-5 FreeVehicle emitted nonpositive or nonfinite "
                           "engine motion");
                    if (sample_index < *release) {
                        expect(sample->engine_speed_rpm == 1500.0,
                               "radial-5 FreeVehicle preparation lost its exact "
                               "1500 RPM hold");
                    } else {
                        observed_released_motion = true;
                    }
                }
                observed_frames += block.frame_count();
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&published)) {
            throw std::runtime_error{"radial-5 FreeVehicle capture faulted (" +
                                     failure->detail_code +
                                     "): " + failure->state_summary};
        }
        expect(!std::holds_alternative<simulation::LowOrderCaptureCompleted>(published),
               "radial-5 FreeVehicle completed before its smoke horizon");

        const auto state = session.free_vehicle_state();
        if (observed_frames <= *release) {
            expect(!state.has_value(),
                   "radial-5 FreeVehicle exposed a sidecar during held "
                   "preparation");
            continue;
        }
        expect(state.has_value() && state->has_committed_drivetrain_step &&
                   std::isfinite(state->engine_speed_rpm) &&
                   state->engine_speed_rpm > 0.0 &&
                   std::isfinite(state->vehicle_speed_m_s) &&
                   state->vehicle_speed_m_s > 0.0 &&
                   std::isfinite(state->vehicle_distance_m) &&
                   state->vehicle_distance_m >= 0.0 &&
                   state->selected_forward_gear_ordinal == 1U,
               "radial-5 FreeVehicle omitted finite released drivetrain "
               "telemetry");
        if (observed_frames > 13000U) {
            expect(state->clutch_engagement_01 == 0.2 &&
                       state->clutch_torque_capacity_nm > 0.0 &&
                       state->applied_average_clutch_torque_on_engine_nm != 0.0 &&
                       state->requested_road_load_force_n > 0.0 &&
                       state->applied_average_road_load_force_n > 0.0,
                   "radial-5 FreeVehicle did not apply its authored 20% clutch "
                   "and road load");
            observed_loaded_clutch = true;
        }
    }
    expect(observed_frames == kSmokeFrameCount &&
               session.published_sample_count() == kSmokeFrameCount &&
               !session.completed() && !session.faulted() && observed_released_motion &&
               observed_loaded_clutch,
           "radial-5 FreeVehicle did not hold, release, and engage its 20% "
           "clutch load during the focused smoke");
}

void verify_public_held_dyno_capture(const std::filesystem::path &repository_root) {
    const auto fixture = test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/"
        "held-dyno-pull-hold-lift-1500-2800rpm.json");
    const auto control_fixture = test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/"
        "prescribed-pull-hold-lift-1500-2800rpm.json");
    const auto *dyno = std::get_if<contract::HeldDyno>(&fixture.scenario.mode);
    const auto *control =
        std::get_if<contract::PrescribedKinematicSweep>(&control_fixture.scenario.mode);
    const auto *control_rpm =
        control == nullptr
            ? nullptr
            : std::get_if<contract::FixedRateRpmTrajectory>(&control->trajectory.rpm);
    const auto horizon = contract::resolve_frame_index(
        fixture.scenario.total_duration_s.value, fixture.scenario.rates.physics);
    const auto release = contract::resolve_frame_index(
        fixture.scenario.audible_start_s.value, fixture.scenario.rates.physics);
    expect(
        dyno != nullptr && horizon.has_value() && *horizon == 120000U &&
            release.has_value() && *release == 10000U &&
            dyno->constraint_method.value ==
                simulation::
                    bounded_held_dyno_one_level_master_rod_constraint_method_identity() &&
            dyno->target_engine_speed_rpm.post_step_rpm.size() == *horizon,
        "radial-5 public HeldDyno fixture lost its method or exact frame grid");
    expect(control != nullptr && control_rpm != nullptr &&
               control_rpm->post_step_rpm ==
                   dyno->target_engine_speed_rpm.post_step_rpm &&
               std::ranges::equal(control->throttle_01.points, dyno->throttle_01.points,
                                  [](const auto &left, const auto &right) {
                                      return left.time_s == right.time_s &&
                                             left.value == right.value;
                                  }) &&
               control_fixture.scenario.audible_start_s.value ==
                   fixture.scenario.audible_start_s.value &&
               control_fixture.scenario.audible_duration_s.value ==
                   fixture.scenario.audible_duration_s.value,
           "radial-5 prescribed listening control differs from the HeldDyno target "
           "or throttle procedure");

    auto wrong_method_scenario = fixture.scenario;
    std::get<contract::HeldDyno>(wrong_method_scenario.mode).constraint_method.value =
        simulation::bounded_held_dyno_constraint_method_identity();
    const auto wrong_method_result = simulation::compile_low_order_capture_session(
        fixture.engine, wrong_method_scenario,
        test::compile_fixture_random_plan(fixture), nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
    const auto *wrong_method_report =
        std::get_if<contract::ValidationReport>(&wrong_method_result);
    expect(wrong_method_report != nullptr &&
               has_validation_issue(
                   *wrong_method_report, contract::ContractIssueCode::unsupported_value,
                   "scenario.mode.constraint_method.value", "mechanism-family"),
           "radial-5 HeldDyno admitted the direct centered-slider constraint "
           "identity");

    auto result = simulation::compile_low_order_capture_session(
        fixture.engine, fixture.scenario, test::compile_fixture_random_plan(fixture),
        nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        throw std::runtime_error{"radial-5 public HeldDyno admission failed: " +
                                 diagnostics(*report)};
    }
    auto session = std::get<simulation::LowOrderCaptureSession>(std::move(result));

    std::uint64_t observed_frames = 0U;
    double maximum_pull_tracking_error_rpm = 0.0;
    double plateau_rpm = 0.0;
    double first_lift_throttle = 0.0;
    double final_rpm = 0.0;
    bool observed_finite_released_motion = false;
    bool observed_positive_absorbing_reaction = false;
    bool observed_released_sidecar = false;
    while (true) {
        auto published =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                for (std::size_t index = 0U; index < block.frame_count(); ++index) {
                    const auto sample_index = block.clock().first_sample_index + index;
                    const auto *sample = block.engine_sample(index);
                    expect(sample != nullptr && sample->dyno_enabled &&
                               !sample->starter_enabled &&
                               std::isfinite(sample->engine_speed_rpm) &&
                               sample->engine_speed_rpm > 0.0,
                           "radial-5 HeldDyno emitted invalid positive-speed motion");
                    if (sample_index < *release) {
                        expect(sample->engine_speed_rpm == 1500.0,
                               "radial-5 HeldDyno preparation lost its exact 1500 "
                               "RPM hold");
                        continue;
                    }

                    observed_finite_released_motion = true;
                    expect(sample->torque.actuator.availability ==
                                   contract::Availability::available &&
                               sample->torque.dyno_reaction.availability ==
                                   contract::Availability::available &&
                               std::isfinite(sample->torque.actuator.value_nm) &&
                               std::isfinite(sample->torque.dyno_reaction.value_nm) &&
                               sample->torque.dyno_reaction.value_nm ==
                                   -sample->torque.actuator.value_nm,
                           "radial-5 HeldDyno lost exact finite actuator/reaction "
                           "telemetry");
                    observed_positive_absorbing_reaction =
                        observed_positive_absorbing_reaction ||
                        sample->torque.dyno_reaction.value_nm > 0.0;
                    if (sample_index < 62000U) {
                        maximum_pull_tracking_error_rpm =
                            std::max(maximum_pull_tracking_error_rpm,
                                     std::abs(sample->engine_speed_rpm -
                                              dyno->target_engine_speed_rpm
                                                  .post_step_rpm[sample_index]));
                    }
                    if (sample_index == 70000U) {
                        plateau_rpm = sample->engine_speed_rpm;
                    }
                    if (sample_index == 80000U) {
                        first_lift_throttle = sample->requested_throttle_01;
                    }
                    final_rpm = sample->engine_speed_rpm;
                }
                observed_frames += block.frame_count();
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&published)) {
            throw std::runtime_error{"radial-5 HeldDyno capture faulted (" +
                                     failure->detail_code +
                                     "): " + failure->state_summary};
        }
        if (const auto *completed =
                std::get_if<simulation::LowOrderCaptureCompleted>(&published)) {
            expect(completed->sample_count == *horizon && observed_frames == *horizon &&
                       session.completed() && !session.faulted() &&
                       observed_released_sidecar,
                   "radial-5 HeldDyno capture did not complete exactly 120000 "
                   "frames");
            break;
        }
        const auto state = session.held_dyno_state();
        if (observed_frames <= *release) {
            expect(!state.has_value(),
                   "radial-5 HeldDyno exposed a sidecar during held preparation");
        } else {
            expect(state.has_value() && std::isfinite(state->target_engine_speed_rpm) &&
                       std::isfinite(state->required_actuator_torque_nm) &&
                       std::isfinite(state->applied_actuator_torque_nm),
                   "radial-5 HeldDyno omitted its finite released sidecar");
            observed_released_sidecar = true;
        }
    }
    expect(observed_finite_released_motion && observed_positive_absorbing_reaction &&
               maximum_pull_tracking_error_rpm < 25.0 &&
               std::abs(plateau_rpm - 2800.0) < 25.0 && first_lift_throttle == 0.08 &&
               final_rpm > 0.0 && final_rpm < plateau_rpm,
           "radial-5 HeldDyno did not execute its pull, plateau, and lift behavior");
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
                       block->physics_frame_count() == 400U &&
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
                   completed.physics_frame_count == 1600U &&
                   completed.delivery_frame_count == 15360U &&
                   !completed.live_controls_accepted && observed_nonzero_audio,
               "radial-5 public audio session did not complete with finite PCM");
        break;
    }

    auto free_scenario =
        require(compile::compile_scenario(engine, source.free_engine_scenario_document),
                "radial-5 public FreeEngine scenario compilation failed");
    auto free_created = engine_sim_offline::create_engine_session(
        free_scenario, engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&free_created)) {
        throw std::runtime_error{
            "radial-5 public FreeEngine session creation failed: " +
            error->detail_code + ": " + error->message};
    }
    auto free_session =
        std::get<engine_sim_offline::EngineSession>(std::move(free_created));
    const auto free_descriptor = free_session.descriptor();
    expect(free_descriptor.engine_id == "radial-5-cleanroom" &&
               free_descriptor.motion_mode ==
                   engine_sim_offline::EngineMotionMode::free_engine &&
               free_descriptor.preparation_block_count == 25U &&
               free_descriptor.total_block_count == 260U &&
               free_descriptor.live_control_capabilities != 0U,
           "radial-5 public FreeEngine descriptor lost its warm/live surface");
    std::uint64_t free_block_count = 0U;
    bool observed_free_audible_audio = false;
    while (true) {
        auto result = free_session.process_block();
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
            throw std::runtime_error{"radial-5 public FreeEngine session faulted: " +
                                     error->detail_code + ": " + error->message};
        }
        if (const auto *block =
                std::get_if<engine_sim_offline::EngineSessionBlockView>(&result)) {
            const auto expected_phase =
                free_block_count < free_descriptor.preparation_block_count
                    ? engine_sim_offline::EngineSessionBlockPhase::preparation
                    : engine_sim_offline::EngineSessionBlockPhase::audible;
            expect(block->block_ordinal() == free_block_count &&
                       block->phase() == expected_phase &&
                       std::ranges::all_of(block->audio_buses(),
                                           [](const auto &bus) {
                                               return std::ranges::all_of(
                                                   bus.samples, [](float sample) {
                                                       return std::isfinite(sample);
                                                   });
                                           }),
                   "radial-5 public FreeEngine emitted a malformed or nonfinite "
                   "block");
            if (expected_phase ==
                engine_sim_offline::EngineSessionBlockPhase::audible) {
                observed_free_audible_audio =
                    observed_free_audible_audio ||
                    std::ranges::any_of(block->audio_buses(), [](const auto &bus) {
                        return std::ranges::any_of(
                            bus.samples, [](float sample) { return sample != 0.0F; });
                    });
            }
            ++free_block_count;
            continue;
        }
        const auto &completed =
            std::get<engine_sim_offline::EngineSessionCompleted>(result);
        expect(free_block_count == free_descriptor.total_block_count &&
                   completed.block_count == free_block_count &&
                   completed.physics_frame_count == 104000U &&
                   completed.delivery_frame_count == 998400U &&
                   observed_free_audible_audio,
               "radial-5 public FreeEngine did not complete its full authored "
               "throttle procedure with finite nonzero PCM");
        break;
    }

    auto held_dyno_scenario =
        require(compile::compile_scenario(engine, source.held_dyno_scenario_document),
                "radial-5 public HeldDyno scenario compilation failed");
    auto held_dyno_created = engine_sim_offline::create_engine_session(
        held_dyno_scenario,
        engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&held_dyno_created)) {
        throw std::runtime_error{"radial-5 public HeldDyno session creation failed: " +
                                 error->detail_code + ": " + error->message};
    }
    auto held_dyno_session =
        std::get<engine_sim_offline::EngineSession>(std::move(held_dyno_created));
    const auto held_dyno_descriptor = held_dyno_session.descriptor();
    expect(held_dyno_descriptor.engine_id == "radial-5-cleanroom" &&
               held_dyno_descriptor.motion_mode ==
                   engine_sim_offline::EngineMotionMode::held_dyno &&
               held_dyno_descriptor.preparation_block_count == 25U &&
               held_dyno_descriptor.total_block_count == 300U &&
               (held_dyno_descriptor.live_control_capabilities &
                engine_sim_offline::
                    kEngineLiveControlCapabilityHeldDynoTargetEngineSpeed) != 0U,
           "radial-5 public HeldDyno descriptor lost its finite warm/live surface");
    std::uint64_t held_dyno_block_count = 0U;
    bool observed_held_dyno_audible_audio = false;
    bool observed_held_dyno_sidecar = false;
    constexpr std::uint64_t kHeldDynoSmokeBlockCount = 30U;
    while (held_dyno_block_count < kHeldDynoSmokeBlockCount) {
        auto result = held_dyno_session.process_block();
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
            throw std::runtime_error{"radial-5 public HeldDyno session faulted: " +
                                     error->detail_code + ": " + error->message};
        }
        if (const auto *block =
                std::get_if<engine_sim_offline::EngineSessionBlockView>(&result)) {
            const auto expected_phase =
                held_dyno_block_count < held_dyno_descriptor.preparation_block_count
                    ? engine_sim_offline::EngineSessionBlockPhase::preparation
                    : engine_sim_offline::EngineSessionBlockPhase::audible;
            expect(block->block_ordinal() == held_dyno_block_count &&
                       block->phase() == expected_phase &&
                       std::ranges::all_of(block->audio_buses(),
                                           [](const auto &bus) {
                                               return std::ranges::all_of(
                                                   bus.samples, [](float sample) {
                                                       return std::isfinite(sample);
                                                   });
                                           }),
                   "radial-5 public HeldDyno emitted a malformed or nonfinite "
                   "block");
            if (expected_phase ==
                engine_sim_offline::EngineSessionBlockPhase::audible) {
                observed_held_dyno_audible_audio =
                    observed_held_dyno_audible_audio ||
                    std::ranges::any_of(block->audio_buses(), [](const auto &bus) {
                        return std::ranges::any_of(
                            bus.samples, [](float sample) { return sample != 0.0F; });
                    });
                expect(block->telemetry().size() == 1U &&
                           block->telemetry().front().held_dyno.has_value() &&
                           std::isfinite(block->telemetry()
                                             .front()
                                             .held_dyno->required_actuator_torque_nm) &&
                           std::isfinite(block->telemetry()
                                             .front()
                                             .held_dyno->applied_actuator_torque_nm),
                       "radial-5 public HeldDyno omitted its finite audible "
                       "telemetry sidecar");
                observed_held_dyno_sidecar = true;
            }
            ++held_dyno_block_count;
            continue;
        }
        throw std::runtime_error{
            "radial-5 public HeldDyno completed before its finite-session smoke "
            "horizon"};
    }
    expect(observed_held_dyno_audible_audio && observed_held_dyno_sidecar,
           "radial-5 public HeldDyno did not cross preparation into finite nonzero "
           "audible PCM");
}

void run(const std::filesystem::path &repository_root) {
    const auto source = load_source(repository_root);
    verify_authored_associations(source.engine_document);
    verify_resolved_topology(source.resolved);
    verify_scenario_resolution_and_mode_gate(source);
    verify_public_capture(repository_root);
    verify_public_free_vehicle_capture(repository_root);
    verify_public_held_dyno_capture(repository_root);
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
