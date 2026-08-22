#include "simulation/crankwave_transient_friction.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/one_level_master_rod_configuration_inertia.hpp"
#include "simulation/one_level_master_rod_coupled_reaction.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace crankwave::contract;
using namespace crankwave::simulation;

static_assert(
    !std::is_copy_assignable_v<CompiledOneLevelMasterRodArticulatedMechanism>);
static_assert(
    !std::is_move_assignable_v<CompiledOneLevelMasterRodArticulatedMechanism>);

void expect(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void expect_near(const double actual, const double expected,
                 const double absolute_tolerance, const double relative_tolerance,
                 const char *message) {
    const double tolerance =
        absolute_tolerance + relative_tolerance * std::abs(expected);
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > tolerance) {
        std::cerr << message << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << '\n';
        throw std::runtime_error{message};
    }
}

constexpr double inch() {
    return 0.0254;
}

constexpr double cubic_centimeter() {
    return 1.0e-6;
}

struct FixtureGeometry {
    double bore_m = 5.0 * inch();
    double piston_area_m2 = kLegacyPi * bore_m * bore_m / 4.0;
    double deck_height_m = 15.75 * inch();
    double compression_height_m = 1.0 * inch();
    double head_volume_m3 = 290.0 * cubic_centimeter();
};

[[nodiscard]] OneLevelMasterRodMechanismCylinderPlan
planned_cylinder(const std::size_t stable_index,
                 const OneLevelMasterRodCylinder &geometry,
                 OneLevelMasterRodCylinderKinematicsPlan kinematics,
                 const double piston_mass_kg = 0.2, const double rod_mass_kg = 0.1,
                 const double rod_inertia_kg_m2 = 0.0015884918028487504,
                 const double center_fraction = 0.5) {
    FixtureGeometry fixture;
    OneLevelMasterRodMechanismCylinderPlan result;
    result.crankshaft_id = CrankshaftId{1};
    result.bank_id = BankId{static_cast<std::uint32_t>(stable_index + 1U)};
    result.chamber_volume_id =
        GasVolumeId{static_cast<std::uint32_t>(100U + stable_index)};
    result.exhaust_route_id = RouteId{static_cast<std::uint32_t>(200U + stable_index)};
    result.bore_m = fixture.bore_m;
    result.piston_area_m2 = geometry.piston_area_m2;
    result.fixed_geometry_volume_m3 =
        geometry.head_chamber_volume_m3 +
        geometry.piston_area_m2 *
            (geometry.deck_height_m - geometry.piston_wrist_pin_position_m -
             geometry.piston_compression_height_m);
    result.ignition_wire_angle_rad = 0.0;
    result.piston_mass_kg = piston_mass_kg;
    result.connecting_rod_mass_kg = rod_mass_kg;
    result.connecting_rod_inertia_kg_m2 = rod_inertia_kg_m2;
    result.connecting_rod_center_of_mass_from_big_end_m =
        center_fraction * geometry.connecting_rod_length_m;
    result.kinematics = std::move(kinematics);
    return result;
}

[[nodiscard]] OneLevelMasterRodMechanismKinematicsPlan
direct_root_plan(const double center_fraction = 0.37) {
    FixtureGeometry fixture;
    constexpr double crank_radius_m = 0.05;
    constexpr double rod_length_m = 0.2;
    const OneLevelMasterRodDriver driver{
        crank_radius_m,
        0.0,
        0.0,
        rod_length_m,
    };
    const OneLevelMasterRodCylinder geometry{
        CylinderId{1},
        0.0,
        rod_length_m,
        fixture.piston_area_m2,
        fixture.deck_height_m,
        fixture.compression_height_m,
        0.0,
        fixture.head_volume_m3,
        0.0,
        OneLevelMasterRodRootJournal{},
    };
    OneLevelMasterRodMechanismKinematicsPlan plan;
    plan.engine_id = EngineId{1};
    plan.engine_profile_id = "direct-root-reaction";
    plan.output_crankshaft_id = CrankshaftId{1};
    plan.crank_tdc_reference_rad = kLegacyPi / 2.0;
    plan.rigid_crank_group = {1U, 0.2, 0.0};
    plan.cylinders.push_back(planned_cylinder(
        0U, geometry, OneLevelMasterRodDirectRootPlan{driver, geometry}, 0.31, 0.22,
        0.0017, center_fraction));
    return plan;
}

[[nodiscard]] OneLevelMasterRodMechanismKinematicsPlan
root_slave_plan(const bool slave_first = false) {
    FixtureGeometry fixture;
    constexpr double degree = kLegacyPi / 180.0;
    const OneLevelMasterRodDriver driver{0.055, 0.0, 0.0, 0.24};
    const OneLevelMasterRodCylinder root_geometry{
        CylinderId{1},
        0.0,
        0.24,
        fixture.piston_area_m2,
        fixture.deck_height_m,
        fixture.compression_height_m,
        0.0,
        fixture.head_volume_m3,
        0.0,
        OneLevelMasterRodRootJournal{},
    };
    const OneLevelMasterRodCylinder slave_geometry{
        CylinderId{2},
        105.0 * degree,
        0.205,
        fixture.piston_area_m2,
        fixture.deck_height_m,
        fixture.compression_height_m,
        0.0,
        fixture.head_volume_m3,
        0.0,
        OneLevelMasterRodSlavePin{0.047, 105.0 * degree},
    };
    OneLevelMasterRodMechanismKinematicsPlan plan;
    plan.engine_id = EngineId{2};
    plan.engine_profile_id =
        slave_first ? "slave-first-reaction" : "root-first-reaction";
    plan.output_crankshaft_id = CrankshaftId{1};
    plan.crank_tdc_reference_rad = 0.91;
    plan.rigid_crank_group = {1U, 0.25, 0.0};
    if (slave_first) {
        plan.cylinders.push_back(
            planned_cylinder(0U, slave_geometry,
                             OneLevelMasterRodSlaveAttachmentPlan{1U, slave_geometry},
                             0.28, 0.19, 0.0012, 0.43));
        plan.cylinders.push_back(planned_cylinder(
            1U, root_geometry, OneLevelMasterRodDirectRootPlan{driver, root_geometry},
            0.35, 0.27, 0.0021, 0.46));
    } else {
        plan.cylinders.push_back(planned_cylinder(
            0U, root_geometry, OneLevelMasterRodDirectRootPlan{driver, root_geometry},
            0.35, 0.27, 0.0021, 0.46));
        plan.cylinders.push_back(
            planned_cylinder(1U, slave_geometry,
                             OneLevelMasterRodSlaveAttachmentPlan{0U, slave_geometry},
                             0.28, 0.19, 0.0012, 0.43));
    }
    return plan;
}

[[nodiscard]] OneLevelMasterRodMechanismKinematicsPlan radial_five_plan() {
    FixtureGeometry fixture;
    constexpr double degree = kLegacyPi / 180.0;
    const OneLevelMasterRodDriver driver{
        2.75 * inch(),
        0.0,
        0.0,
        12.0 * inch(),
    };
    const OneLevelMasterRodCylinder root_geometry{
        CylinderId{1},
        0.0,
        12.0 * inch(),
        fixture.piston_area_m2,
        fixture.deck_height_m,
        fixture.compression_height_m,
        0.0,
        fixture.head_volume_m3,
        0.0,
        OneLevelMasterRodRootJournal{},
    };
    OneLevelMasterRodMechanismKinematicsPlan plan;
    plan.engine_id = EngineId{3};
    plan.engine_profile_id = "pristine-radial-five-reaction";
    plan.output_crankshaft_id = CrankshaftId{1};
    plan.crank_tdc_reference_rad = 67.5 * degree;
    plan.rigid_crank_group = {1U, 0.8581916343875, 0.0};
    plan.cylinders.push_back(planned_cylinder(
        0U, root_geometry, OneLevelMasterRodDirectRootPlan{driver, root_geometry}));
    for (std::size_t index = 1U; index < 5U; ++index) {
        const double phase_rad = static_cast<double>(index) * 72.0 * degree;
        const OneLevelMasterRodCylinder slave_geometry{
            CylinderId{static_cast<std::uint32_t>(index + 1U)},
            phase_rad,
            9.1 * inch(),
            fixture.piston_area_m2,
            fixture.deck_height_m,
            fixture.compression_height_m,
            0.0,
            fixture.head_volume_m3,
            0.0,
            OneLevelMasterRodSlavePin{2.9 * inch(), phase_rad},
        };
        plan.cylinders.push_back(
            planned_cylinder(index, slave_geometry,
                             OneLevelMasterRodSlaveAttachmentPlan{0U, slave_geometry}));
    }
    return plan;
}

[[nodiscard]] CompiledOneLevelMasterRodArticulatedMechanism
require_compiled(OneLevelMasterRodArticulatedMechanismCompilation compilation) {
    expect(std::holds_alternative<CompiledOneLevelMasterRodArticulatedMechanism>(
               compilation),
           "valid master-rod plan did not compile");
    return std::get<CompiledOneLevelMasterRodArticulatedMechanism>(
        std::move(compilation));
}

[[nodiscard]] OneLevelMasterRodArticulatedState
state_at(const CompiledOneLevelMasterRodArticulatedMechanism &compiled,
         const double theta_rad) {
    auto state = compiled.make_state_scratch();
    const auto error = compiled.evaluate_articulated_state(theta_rad, state);
    expect(!error.has_value(), "compiled articulated state failed");
    return state;
}

[[nodiscard]] OneLevelMasterRodConfigurationInertia
require_inertia(const OneLevelMasterRodConfigurationInertiaCalculation &calculation) {
    const auto *result =
        std::get_if<OneLevelMasterRodConfigurationInertia>(&calculation);
    expect(result != nullptr, "valid master-rod inertia evaluation failed");
    return *result;
}

[[nodiscard]] OneLevelMasterRodFrictionStageResult
require_stage(const OneLevelMasterRodFrictionStageCalculation &calculation) {
    const auto *result =
        std::get_if<OneLevelMasterRodFrictionStageResult>(&calculation);
    expect(result != nullptr, "valid radial friction stage was rejected");
    return *result;
}

[[nodiscard]] OneLevelMasterRodCoupledReactionResult
require_reaction(const OneLevelMasterRodCoupledReactionCalculation &calculation) {
    const auto *result =
        std::get_if<OneLevelMasterRodCoupledReactionResult>(&calculation);
    expect(result != nullptr, "valid coupled reaction was rejected");
    return *result;
}

void expect_reaction_issue(
    const OneLevelMasterRodCoupledReactionCalculation &calculation,
    const OneLevelMasterRodCoupledReactionIssue expected, const char *message) {
    const auto *error =
        std::get_if<OneLevelMasterRodCoupledReactionError>(&calculation);
    expect(error != nullptr && error->issue == expected, message);
}

void expect_stage_issue(const OneLevelMasterRodFrictionStageCalculation &calculation,
                        const OneLevelMasterRodCoupledReactionIssue expected,
                        const char *message) {
    const auto *error =
        std::get_if<OneLevelMasterRodCoupledReactionError>(&calculation);
    expect(error != nullptr && error->issue == expected, message);
}

[[nodiscard]] CrankwavePistonWallFrictionStage
require_direct_stage(const CrankwavePistonWallFrictionCalculation &calculation) {
    const auto *result = std::get_if<CrankwavePistonWallFrictionStage>(&calculation);
    expect(result != nullptr, "valid direct friction stage was rejected");
    return *result;
}

[[nodiscard]] CrankwavePistonWallReaction
require_direct_reaction(const CrankwavePistonWallReactionCalculation &calculation) {
    const auto *result = std::get_if<CrankwavePistonWallReaction>(&calculation);
    expect(result != nullptr, "valid direct wall reaction was rejected");
    return *result;
}

[[nodiscard]] std::vector<OneLevelMasterRodPistonWallBoundaryInput>
equal_boundaries(const CompiledOneLevelMasterRodArticulatedMechanism &compiled,
                 const double pressure_pa = 101325.0,
                 const double retained_wall_n = 0.0) {
    std::vector<OneLevelMasterRodPistonWallBoundaryInput> result;
    result.reserve(compiled.cylinder_count());
    for (const auto &view : compiled.cylinder_views()) {
        result.push_back({view.cylinder_id, pressure_pa, pressure_pa, retained_wall_n});
    }
    return result;
}

void test_direct_root_reduces_to_existing_centered_reaction() {
    constexpr double piston_area_m2 = FixtureGeometry{}.piston_area_m2;
    constexpr double crank_radius_m = 0.05;
    constexpr double rod_length_m = 0.2;
    constexpr double center_fraction = 0.37;
    constexpr double piston_mass_kg = 0.31;
    constexpr double rod_mass_kg = 0.22;
    constexpr double rod_inertia_kg_m2 = 0.0017;
    constexpr double crankcase_pressure_pa = 101325.0;
    auto compiled = require_compiled(compile_one_level_master_rod_articulated_mechanism(
        direct_root_plan(center_fraction)));
    auto reaction_workspace =
        make_one_level_master_rod_coupled_reaction_workspace(compiled);

    struct Case {
        double theta;
        double omega;
        double alpha;
        double chamber_pressure;
        double retained_wall;
    };
    constexpr std::array cases{
        Case{0.23, 0.0, 0.0, 240000.0, 0.0},
        Case{1.1, 314.1592653589793, -120.0, 560000.0, 1800.0},
        Case{2.31, 83.0, 41.0, 80000.0, 720.0},
    };
    for (const auto &sample : cases) {
        auto state = state_at(compiled, sample.theta);
        const std::array boundaries{OneLevelMasterRodPistonWallBoundaryInput{
            CylinderId{1}, sample.chamber_pressure, crankcase_pressure_pa,
            sample.retained_wall}};
        const auto &radial_stage =
            require_stage(stage_one_level_master_rod_piston_wall_friction(
                compiled, state, boundaries, sample.omega, reaction_workspace));
        const auto &radial_result =
            require_reaction(calculate_one_level_master_rod_coupled_reactions(
                compiled, state, sample.omega, sample.alpha, reaction_workspace));

        const CrankwavePistonWallCylinderPlan direct_plan{
            piston_area_m2,    crank_radius_m,
            rod_length_m,      center_fraction * rod_length_m,
            piston_mass_kg,    rod_mass_kg,
            rod_inertia_kg_m2, crankcase_pressure_pa,
        };
        const auto &direct_stage =
            require_direct_stage(stage_crankwave_piston_wall_friction({
                direct_plan,
                sample.theta,
                sample.omega,
                sample.chamber_pressure,
                sample.retained_wall,
            }));
        const auto &direct_result =
            require_direct_reaction(calculate_crankwave_next_piston_wall_reaction(
                direct_stage, sample.alpha));
        const auto &radial_reaction = radial_result.cylinders.front();

        expect_near(radial_stage.total_generalized_friction_torque_nm,
                    direct_stage.generalized_friction_torque_nm, 2.0e-13, 2.0e-12,
                    "direct-root radial friction torque differs from centered path");
        expect_near(radial_result.total_generalized_friction_torque_nm,
                    direct_stage.generalized_friction_torque_nm, 2.0e-13, 2.0e-12,
                    "coupled result changed the staged direct friction torque");
        expect_near(radial_reaction.signed_wall_on_piston_force_n,
                    direct_result.signed_wall_on_piston_force_n, 5.0e-8, 2.0e-11,
                    "direct-root signed wall reaction differs from centered path");
        expect_near(radial_reaction.wall_reaction_magnitude_n,
                    direct_result.wall_reaction_magnitude_n, 5.0e-8, 2.0e-11,
                    "direct-root wall magnitude differs from centered path");
    }
}

[[nodiscard]] std::size_t
cylinder_index(const CompiledOneLevelMasterRodArticulatedMechanism &compiled,
               const CylinderId id) {
    const auto views = compiled.cylinder_views();
    for (std::size_t index = 0; index < views.size(); ++index) {
        if (views[index].cylinder_id == id) {
            return index;
        }
    }
    throw std::runtime_error{"compiled cylinder ID missing"};
}

[[nodiscard]] double cross(const OneLevelMasterRodPlanarForce left,
                           const OneLevelMasterRodPlanarForce right) {
    return left.x_n * right.y_n - left.y_n * right.x_n;
}

[[nodiscard]] double dot(const OneLevelMasterRodPlanarForce left,
                         const OneLevelMasterRodPlanarForce right) {
    return left.x_n * right.x_n + left.y_n * right.y_n;
}

[[nodiscard]] OneLevelMasterRodPlanarForce
add(const OneLevelMasterRodPlanarForce left, const OneLevelMasterRodPlanarForce right) {
    return {left.x_n + right.x_n, left.y_n + right.y_n};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
subtract(const OneLevelMasterRodPlanarForce left,
         const OneLevelMasterRodPlanarForce right) {
    return {left.x_n - right.x_n, left.y_n - right.y_n};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
scale(const double scalar, const OneLevelMasterRodPlanarForce value) {
    return {scalar * value.x_n, scalar * value.y_n};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
point_difference(const OneLevelMasterRodPlanarPointState &left,
                 const OneLevelMasterRodPlanarPointState &right) {
    return {left.x_m - right.x_m, left.y_m - right.y_m};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
point_derivative(const OneLevelMasterRodPlanarPointState &point) {
    return {point.dx_dtheta_m_per_rad, point.dy_dtheta_m_per_rad};
}

[[nodiscard]] OneLevelMasterRodPlanarForce
point_velocity(const OneLevelMasterRodPlanarPointState &point,
               const double angular_speed_rad_s) {
    return scale(angular_speed_rad_s, point_derivative(point));
}

[[nodiscard]] OneLevelMasterRodPlanarForce
point_acceleration(const OneLevelMasterRodPlanarPointState &point,
                   const double angular_speed_rad_s,
                   const double angular_acceleration_rad_s2) {
    const double omega_squared = angular_speed_rad_s * angular_speed_rad_s;
    return {
        point.d2x_dtheta2_m_per_rad2 * omega_squared +
            point.dx_dtheta_m_per_rad * angular_acceleration_rad_s2,
        point.d2y_dtheta2_m_per_rad2 * omega_squared +
            point.dy_dtheta_m_per_rad * angular_acceleration_rad_s2,
    };
}

void expect_scaled_zero(const double residual, const double term_scale,
                        const double absolute_tolerance,
                        const double relative_tolerance, const char *message) {
    const double tolerance = absolute_tolerance + relative_tolerance * term_scale;
    if (!std::isfinite(residual) || !std::isfinite(term_scale) ||
        std::abs(residual) > tolerance) {
        std::cerr << message << ": residual=" << residual
                  << " term_scale=" << term_scale << " tolerance=" << tolerance << '\n';
        throw std::runtime_error{message};
    }
}

void test_slave_force_propagates_to_root_and_order_is_irrelevant() {
    struct Result {
        double root_wall = 0.0;
        double slave_wall = 0.0;
        OneLevelMasterRodPlanarForce slave_parent;
        OneLevelMasterRodPlanarForce accumulated_child;
        double accumulated_moment = 0.0;
    };
    const auto evaluate = [](const bool slave_first, const double slave_pressure_pa) {
        auto compiled =
            require_compiled(compile_one_level_master_rod_articulated_mechanism(
                root_slave_plan(slave_first)));
        auto state = state_at(compiled, 1.37);
        auto boundaries = equal_boundaries(compiled);
        const std::size_t root = cylinder_index(compiled, CylinderId{1});
        const std::size_t slave = cylinder_index(compiled, CylinderId{2});
        boundaries[slave].chamber_pressure_pa_abs = slave_pressure_pa;
        auto workspace = make_one_level_master_rod_coupled_reaction_workspace(compiled);
        const auto staged =
            require_stage(stage_one_level_master_rod_piston_wall_friction(
                compiled, state, boundaries, 147.0, workspace));
        const std::vector<OneLevelMasterRodPistonWallFrictionStage> stages_before{
            staged.cylinders.begin(), staged.cylinders.end()};
        const auto resolved =
            require_reaction(calculate_one_level_master_rod_coupled_reactions(
                compiled, state, 147.0, -31.0, workspace));
        expect(stages_before.size() == resolved.cylinders.size(),
               "reaction traversal changed transaction cardinality");

        const auto &root_reaction = resolved.cylinders[root];
        const auto &slave_reaction = resolved.cylinders[slave];
        const OneLevelMasterRodPlanarForce expected_child{
            -slave_reaction.parent_on_rod_at_big_end_n.x_n,
            -slave_reaction.parent_on_rod_at_big_end_n.y_n,
        };
        expect(root_reaction.accumulated_child_force_on_rod_n == expected_child,
               "slave big-end force did not propagate exactly to its root");
        const OneLevelMasterRodPlanarForce pin_from_root{
            state.cylinders[slave].big_end.x_m - state.cylinders[root].big_end.x_m,
            state.cylinders[slave].big_end.y_m - state.cylinders[root].big_end.y_m,
        };
        expect_near(root_reaction.accumulated_child_moment_about_big_end_nm,
                    cross(pin_from_root, expected_child), 1.0e-13, 1.0e-13,
                    "slave force moment did not propagate about the root big end");
        return Result{
            root_reaction.signed_wall_on_piston_force_n,
            slave_reaction.signed_wall_on_piston_force_n,
            slave_reaction.parent_on_rod_at_big_end_n,
            root_reaction.accumulated_child_force_on_rod_n,
            root_reaction.accumulated_child_moment_about_big_end_nm,
        };
    };

    const auto loaded = evaluate(false, 430000.0);
    const auto unloaded = evaluate(false, 101325.0);
    expect(std::abs(loaded.root_wall - unloaded.root_wall) > 100.0,
           "slave pressure load did not change the master wall reaction");

    const auto reordered = evaluate(true, 430000.0);
    expect_near(reordered.root_wall, loaded.root_wall, 1.0e-9, 1.0e-12,
                "slave-before-root ordering changed the root reaction");
    expect_near(reordered.slave_wall, loaded.slave_wall, 1.0e-9, 1.0e-12,
                "slave-before-root ordering changed the slave reaction");
    expect_near(reordered.slave_parent.x_n, loaded.slave_parent.x_n, 1.0e-9, 1.0e-12,
                "slave-before-root ordering changed big-end force x");
    expect_near(reordered.slave_parent.y_n, loaded.slave_parent.y_n, 1.0e-9, 1.0e-12,
                "slave-before-root ordering changed big-end force y");
}

void test_loaded_root_slave_satisfies_independent_newton_euler_balances() {
    struct LoadedCase {
        double theta_rad;
        double angular_speed_rad_s;
        double angular_acceleration_rad_s2;
        double root_pressure_pa;
        double slave_pressure_pa;
        double root_retained_wall_n;
        double slave_retained_wall_n;
    };
    constexpr std::array cases{
        LoadedCase{0.41, 91.0, -37.0, 310000.0, 470000.0, 640.0, 980.0},
        LoadedCase{1.37, 147.0, 23.0, 520000.0, 220000.0, 1250.0, 780.0},
        LoadedCase{4.62, -83.0, 52.0, 85000.0, 610000.0, 310.0, 1430.0},
    };
    constexpr double crankcase_pressure_pa = 101325.0;
    auto compiled = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(root_slave_plan()));
    auto workspace = make_one_level_master_rod_coupled_reaction_workspace(compiled);
    const auto views = compiled.cylinder_views();
    const std::size_t root = cylinder_index(compiled, CylinderId{1});
    const std::size_t slave = cylinder_index(compiled, CylinderId{2});

    for (const auto &sample : cases) {
        auto state = compiled.make_state_scratch();
        const auto inertia = require_inertia(
            compiled.evaluate_configuration_inertia(0.0, sample.theta_rad, state));
        auto boundaries = equal_boundaries(compiled, crankcase_pressure_pa);
        boundaries[root].chamber_pressure_pa_abs = sample.root_pressure_pa;
        boundaries[root].retained_previous_wall_reaction_magnitude_n =
            sample.root_retained_wall_n;
        boundaries[slave].chamber_pressure_pa_abs = sample.slave_pressure_pa;
        boundaries[slave].retained_previous_wall_reaction_magnitude_n =
            sample.slave_retained_wall_n;

        const auto staged =
            require_stage(stage_one_level_master_rod_piston_wall_friction(
                compiled, state, boundaries, sample.angular_speed_rad_s, workspace));
        const std::vector<OneLevelMasterRodPistonWallFrictionStage> stages{
            staged.cylinders.begin(), staged.cylinders.end()};
        expect(stages[root].friction_force_magnitude_n > 0.0 &&
                   stages[slave].friction_force_magnitude_n > 0.0,
               "loaded balance case failed to exercise piston-wall friction");
        const auto resolved =
            require_reaction(calculate_one_level_master_rod_coupled_reactions(
                compiled, state, sample.angular_speed_rad_s,
                sample.angular_acceleration_rad_s2, workspace));

        std::vector<OneLevelMasterRodPlanarForce> independent_child_forces(
            views.size());
        std::vector<double> independent_child_moments_nm(views.size(), 0.0);
        for (std::size_t index = 0; index < views.size(); ++index) {
            if (views[index].direct_root) {
                continue;
            }
            const std::size_t parent = views[index].parent_root_index;
            const auto child_on_parent =
                scale(-1.0, resolved.cylinders[index].parent_on_rod_at_big_end_n);
            independent_child_forces[parent] =
                add(independent_child_forces[parent], child_on_parent);
            const auto pin_from_parent = point_difference(
                state.cylinders[index].big_end, state.cylinders[parent].big_end);
            independent_child_moments_nm[parent] +=
                cross(pin_from_parent, child_on_parent);
        }
        expect_near(resolved.cylinders[root].accumulated_child_force_on_rod_n.x_n,
                    independent_child_forces[root].x_n, 1.0e-10, 1.0e-12,
                    "root child-force diagnostic x differs from independent assembly");
        expect_near(resolved.cylinders[root].accumulated_child_force_on_rod_n.y_n,
                    independent_child_forces[root].y_n, 1.0e-10, 1.0e-12,
                    "root child-force diagnostic y differs from independent assembly");
        expect_near(resolved.cylinders[root].accumulated_child_moment_about_big_end_nm,
                    independent_child_moments_nm[root], 1.0e-10, 1.0e-12,
                    "root child-moment diagnostic differs from independent assembly");

        double generalized_axis_force_nm = 0.0;
        double generalized_joint_force_nm = 0.0;
        double generalized_force_scale_nm = 0.0;
        double external_power_w = 0.0;
        double kinetic_power_w = 0.0;
        double power_scale_w = 0.0;
        double wall_power_w = 0.0;
        double wall_power_scale_w = 0.0;
        for (std::size_t index = 0; index < views.size(); ++index) {
            const auto &view = views[index];
            const auto &cylinder = state.cylinders[index];
            const auto &reaction = resolved.cylinders[index];
            const OneLevelMasterRodPlanarForce axis{view.bank_axis_x, view.bank_axis_y};
            const OneLevelMasterRodPlanarForce normal{
                view.clockwise_normal_x,
                view.clockwise_normal_y,
            };
            const auto rod = point_difference(cylinder.wrist_pin, cylinder.big_end);
            const auto center_from_big_end =
                point_difference(cylinder.rod_center_of_mass, cylinder.big_end);
            const auto piston_acceleration =
                point_acceleration(cylinder.wrist_pin, sample.angular_speed_rad_s,
                                   sample.angular_acceleration_rad_s2);
            const auto rod_center_acceleration = point_acceleration(
                cylinder.rod_center_of_mass, sample.angular_speed_rad_s,
                sample.angular_acceleration_rad_s2);
            const auto piston_velocity =
                point_velocity(cylinder.wrist_pin, sample.angular_speed_rad_s);
            const auto rod_center_velocity =
                point_velocity(cylinder.rod_center_of_mass, sample.angular_speed_rad_s);
            const double rod_angular_velocity =
                cylinder.rod_angle_first_derivative_rad_per_rad *
                sample.angular_speed_rad_s;
            const double rod_angular_acceleration =
                cylinder.rod_angle_second_derivative_rad_per_rad2 *
                    sample.angular_speed_rad_s * sample.angular_speed_rad_s +
                cylinder.rod_angle_first_derivative_rad_per_rad *
                    sample.angular_acceleration_rad_s2;
            const double signed_pressure_force_n =
                -view.piston_area_m2 * (boundaries[index].chamber_pressure_pa_abs -
                                        boundaries[index].crankcase_pressure_pa_abs);
            const double signed_axis_force_n =
                signed_pressure_force_n + stages[index].signed_axis_friction_force_n;
            const auto axis_force = scale(signed_axis_force_n, axis);
            const auto wall_force =
                scale(reaction.signed_wall_on_piston_force_n, normal);
            const auto rod_on_piston = reaction.rod_on_piston_at_wrist_n;
            const auto parent_on_rod = reaction.parent_on_rod_at_big_end_n;
            const auto piston_inertia_force =
                scale(view.piston_mass_kg, piston_acceleration);
            const auto rod_inertia_force =
                scale(view.connecting_rod_mass_kg, rod_center_acceleration);

            const auto piston_residual = subtract(
                add(add(axis_force, wall_force), rod_on_piston), piston_inertia_force);
            const double piston_x_scale =
                std::abs(axis_force.x_n) + std::abs(wall_force.x_n) +
                std::abs(rod_on_piston.x_n) + std::abs(piston_inertia_force.x_n);
            const double piston_y_scale =
                std::abs(axis_force.y_n) + std::abs(wall_force.y_n) +
                std::abs(rod_on_piston.y_n) + std::abs(piston_inertia_force.y_n);
            expect_scaled_zero(piston_residual.x_n, piston_x_scale, 5.0e-10, 5.0e-12,
                               "piston x-force balance failed");
            expect_scaled_zero(piston_residual.y_n, piston_y_scale, 5.0e-10, 5.0e-12,
                               "piston y-force balance failed");

            const auto child_force = independent_child_forces[index];
            const auto rod_residual =
                subtract(add(subtract(parent_on_rod, rod_on_piston), child_force),
                         rod_inertia_force);
            const double rod_x_scale =
                std::abs(parent_on_rod.x_n) + std::abs(rod_on_piston.x_n) +
                std::abs(child_force.x_n) + std::abs(rod_inertia_force.x_n);
            const double rod_y_scale =
                std::abs(parent_on_rod.y_n) + std::abs(rod_on_piston.y_n) +
                std::abs(child_force.y_n) + std::abs(rod_inertia_force.y_n);
            expect_scaled_zero(rod_residual.x_n, rod_x_scale, 5.0e-10, 5.0e-12,
                               "rod x-force balance failed");
            expect_scaled_zero(rod_residual.y_n, rod_y_scale, 5.0e-10, 5.0e-12,
                               "rod y-force balance failed");

            const double wrist_moment = cross(rod, scale(-1.0, rod_on_piston));
            const double center_inertia_moment =
                cross(center_from_big_end, rod_inertia_force);
            const double rotation_inertia_moment =
                view.connecting_rod_inertia_kg_m2 * rod_angular_acceleration;
            const double moment_residual =
                wrist_moment + independent_child_moments_nm[index] -
                center_inertia_moment - rotation_inertia_moment;
            const double moment_scale =
                std::abs(wrist_moment) + std::abs(independent_child_moments_nm[index]) +
                std::abs(center_inertia_moment) + std::abs(rotation_inertia_moment);
            expect_scaled_zero(moment_residual, moment_scale, 5.0e-10, 5.0e-12,
                               "rod moment balance failed");

            const double axis_generalized =
                signed_axis_force_n * dot(axis, point_derivative(cylinder.wrist_pin));
            generalized_axis_force_nm += axis_generalized;
            generalized_force_scale_nm += std::abs(axis_generalized);
            if (view.direct_root) {
                const double joint_generalized =
                    dot(parent_on_rod, point_derivative(cylinder.big_end));
                generalized_joint_force_nm += joint_generalized;
                generalized_force_scale_nm += std::abs(joint_generalized);
            }

            const double boundary_power =
                dot(add(axis_force, wall_force), piston_velocity);
            external_power_w += boundary_power;
            power_scale_w += std::abs(boundary_power);
            if (view.direct_root) {
                const double joint_power =
                    dot(parent_on_rod,
                        point_velocity(cylinder.big_end, sample.angular_speed_rad_s));
                external_power_w += joint_power;
                power_scale_w += std::abs(joint_power);
            }
            const double piston_kinetic_power =
                view.piston_mass_kg * dot(piston_acceleration, piston_velocity);
            const double rod_translation_power =
                view.connecting_rod_mass_kg *
                dot(rod_center_acceleration, rod_center_velocity);
            const double rod_rotation_power = view.connecting_rod_inertia_kg_m2 *
                                              rod_angular_acceleration *
                                              rod_angular_velocity;
            kinetic_power_w +=
                piston_kinetic_power + rod_translation_power + rod_rotation_power;
            power_scale_w += std::abs(piston_kinetic_power) +
                             std::abs(rod_translation_power) +
                             std::abs(rod_rotation_power);
            const double cylinder_wall_power = dot(wall_force, piston_velocity);
            wall_power_w += cylinder_wall_power;
            wall_power_scale_w += std::abs(cylinder_wall_power);
        }

        const double moving_inertia = inertia.piston_translation_inertia_kg_m2 +
                                      inertia.connecting_rod_translation_inertia_kg_m2 +
                                      inertia.connecting_rod_rotation_inertia_kg_m2;
        const double moving_inertia_derivative =
            inertia.piston_translation_derivative_kg_m2_per_rad +
            inertia.connecting_rod_translation_derivative_kg_m2_per_rad +
            inertia.connecting_rod_rotation_derivative_kg_m2_per_rad;
        const double kinetic_generalized_force_nm =
            moving_inertia * sample.angular_acceleration_rad_s2 +
            0.5 * moving_inertia_derivative * sample.angular_speed_rad_s *
                sample.angular_speed_rad_s;
        generalized_force_scale_nm += std::abs(kinetic_generalized_force_nm);
        expect_scaled_zero(generalized_axis_force_nm + generalized_joint_force_nm -
                               kinetic_generalized_force_nm,
                           generalized_force_scale_nm, 2.0e-9, 2.0e-11,
                           "whole-linkage generalized-force balance failed");
        expect_scaled_zero(external_power_w - kinetic_power_w, power_scale_w, 2.0e-7,
                           2.0e-11,
                           "whole-linkage external/kinetic power balance failed");
        const double generalized_kinetic_power_w =
            sample.angular_speed_rad_s * kinetic_generalized_force_nm;
        expect_scaled_zero(kinetic_power_w - generalized_kinetic_power_w,
                           power_scale_w + std::abs(generalized_kinetic_power_w),
                           2.0e-7, 2.0e-11,
                           "configuration-inertia power identity failed");
        expect_scaled_zero(wall_power_w, wall_power_scale_w, 2.0e-7, 2.0e-11,
                           "ideal cylinder wall performed work");
    }
}

void test_radial_five_matches_independent_ideal_wall_oracle() {
    auto compiled = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(radial_five_plan()));
    const double theta_rad = 67.5 * kLegacyPi / 180.0;
    const double omega_rad_s = 600.0 * std::numbers::pi_v<double> / 30.0;
    auto state = state_at(compiled, theta_rad);
    const auto views = compiled.cylinder_views();
    std::vector<OneLevelMasterRodCylinderReaction> reactions(views.size());
    std::vector<OneLevelMasterRodPlanarForce> child_forces(views.size());
    std::vector<double> child_moments(views.size(), 0.0);

    for (std::size_t index = 0; index < views.size(); ++index) {
        if (views[index].direct_root) {
            continue;
        }
        const auto calculation = detail::calculate_one_level_master_rod_branch_reaction(
            views[index], state.cylinders[index], 0.0, 0.0, omega_rad_s, 0.0, {}, 0.0,
            index);
        const auto *reaction =
            std::get_if<OneLevelMasterRodCylinderReaction>(&calculation);
        expect(reaction != nullptr, "radial-five slave reaction failed");
        reactions[index] = *reaction;
        const std::size_t root = views[index].parent_root_index;
        const OneLevelMasterRodPlanarForce child{
            -reaction->parent_on_rod_at_big_end_n.x_n,
            -reaction->parent_on_rod_at_big_end_n.y_n,
        };
        child_forces[root].x_n += child.x_n;
        child_forces[root].y_n += child.y_n;
        const OneLevelMasterRodPlanarForce pin{
            state.cylinders[index].big_end.x_m - state.cylinders[root].big_end.x_m,
            state.cylinders[index].big_end.y_m - state.cylinders[root].big_end.y_m,
        };
        child_moments[root] += cross(pin, child);
    }
    for (std::size_t index = 0; index < views.size(); ++index) {
        if (!views[index].direct_root) {
            continue;
        }
        const auto calculation = detail::calculate_one_level_master_rod_branch_reaction(
            views[index], state.cylinders[index], 0.0, 0.0, omega_rad_s, 0.0,
            child_forces[index], child_moments[index], index);
        const auto *reaction =
            std::get_if<OneLevelMasterRodCylinderReaction>(&calculation);
        expect(reaction != nullptr, "radial-five root reaction failed");
        reactions[index] = *reaction;
    }

    // The independent pristine-geometry CSV used the opposite (CCW) wall normal.
    // These are therefore its signed columns negated into the clean clockwise
    // convention. Its finite-difference derivative oracle warrants 0.01 N.
    constexpr std::array expected_clean_signed_n{
        12.822218491476701, 1.113328255127854,   -21.647618790912635,
        5.4888736720163376, -1.5561382172603817,
    };
    for (std::size_t index = 0; index < reactions.size(); ++index) {
        expect_near(reactions[index].signed_wall_on_piston_force_n,
                    expected_clean_signed_n[index], 0.01, 0.0,
                    "radial-five signed ideal wall oracle changed");
        expect_near(reactions[index].wall_reaction_magnitude_n,
                    std::abs(expected_clean_signed_n[index]), 0.01, 0.0,
                    "radial-five ideal wall magnitude oracle changed");
    }
}

void test_friction_is_dissipative_causal_and_malformed_inputs_fail_closed() {
    auto compiled = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(direct_root_plan()));
    auto state = state_at(compiled, 0.83);
    const std::array boundaries{OneLevelMasterRodPistonWallBoundaryInput{
        CylinderId{1}, 180000.0, 101325.0, 900.0}};
    auto workspace = make_one_level_master_rod_coupled_reaction_workspace(compiled);

    for (const double omega : {-140.0, 0.0, 140.0}) {
        const auto &stage =
            require_stage(stage_one_level_master_rod_piston_wall_friction(
                compiled, state, boundaries, omega, workspace));
        expect(stage.total_generalized_friction_torque_nm * omega <= 1.0e-12,
               "radial piston-wall friction generated energy");
    }

    auto negative_boundary = boundaries;
    negative_boundary.front().retained_previous_wall_reaction_magnitude_n = -1.0;
    const auto negative = stage_one_level_master_rod_piston_wall_friction(
        compiled, state, negative_boundary, 140.0, workspace);
    const auto *negative_error =
        std::get_if<OneLevelMasterRodCoupledReactionError>(&negative);
    expect(
        negative_error != nullptr &&
            negative_error->issue ==
                OneLevelMasterRodCoupledReactionIssue::negative_retained_wall_reaction,
        "negative retained wall magnitude did not fail closed");

    (void)require_stage(stage_one_level_master_rod_piston_wall_friction(
        compiled, state, boundaries, 140.0, workspace));
    const auto nonfinite = calculate_one_level_master_rod_coupled_reactions(
        compiled, state, 140.0, std::numeric_limits<double>::quiet_NaN(), workspace);
    const auto *nonfinite_error =
        std::get_if<OneLevelMasterRodCoupledReactionError>(&nonfinite);
    expect(
        nonfinite_error != nullptr &&
            nonfinite_error->issue ==
                OneLevelMasterRodCoupledReactionIssue::nonfinite_angular_acceleration,
        "nonfinite angular acceleration did not fail closed");
}

void test_workspace_transactions_are_mechanism_and_state_bound() {
    auto compiled_a = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(root_slave_plan()));
    auto compiled_b = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(root_slave_plan()));
    auto state_a = state_at(compiled_a, 0.71);
    auto state_a_later = state_at(compiled_a, 0.72);
    auto state_b = state_at(compiled_b, 0.71);
    auto boundaries_a = equal_boundaries(compiled_a, 180000.0, 400.0);
    auto boundaries_b = equal_boundaries(compiled_b, 180000.0, 400.0);
    auto workspace = make_one_level_master_rod_coupled_reaction_workspace(compiled_a);

    expect_reaction_issue(calculate_one_level_master_rod_coupled_reactions(
                              compiled_a, state_a, 91.0, -3.0, workspace),
                          OneLevelMasterRodCoupledReactionIssue::missing_friction_stage,
                          "resolve-before-stage did not fail closed");

    expect_stage_issue(stage_one_level_master_rod_piston_wall_friction(
                           compiled_b, state_b, boundaries_b, 91.0, workspace),
                       OneLevelMasterRodCoupledReactionIssue::workspace_owner_mismatch,
                       "same-shaped foreign mechanism reused a reaction workspace");

    (void)require_stage(stage_one_level_master_rod_piston_wall_friction(
        compiled_a, state_a, boundaries_a, 91.0, workspace));
    expect_reaction_issue(
        calculate_one_level_master_rod_coupled_reactions(compiled_a, state_a_later,
                                                         91.0, -3.0, workspace),
        OneLevelMasterRodCoupledReactionIssue::staged_state_mismatch,
        "a friction stage was reused at a different articulated state");

    (void)require_stage(stage_one_level_master_rod_piston_wall_friction(
        compiled_a, state_a, boundaries_a, 91.0, workspace));
    expect_reaction_issue(
        calculate_one_level_master_rod_coupled_reactions(
            compiled_a, state_a, std::nextafter(91.0, 92.0), -3.0, workspace),
        OneLevelMasterRodCoupledReactionIssue::staged_state_mismatch,
        "a friction stage was reused at a different angular speed");

    auto move_source = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(root_slave_plan()));
    auto move_state = state_at(move_source, 0.51);
    auto move_boundaries = equal_boundaries(move_source, 170000.0, 300.0);
    auto move_workspace =
        make_one_level_master_rod_coupled_reaction_workspace(move_source);
    (void)require_stage(stage_one_level_master_rod_piston_wall_friction(
        move_source, move_state, move_boundaries, 73.0, move_workspace));
    auto moved_mechanism = std::move(move_source);
    expect_reaction_issue(
        calculate_one_level_master_rod_coupled_reactions(moved_mechanism, move_state,
                                                         73.0, 4.0, move_workspace),
        OneLevelMasterRodCoupledReactionIssue::workspace_owner_mismatch,
        "moving a compiled mechanism silently transferred workspace ownership");
}

void test_failed_late_stage_invalidates_the_whole_transaction() {
    auto compiled = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(root_slave_plan()));
    auto state = state_at(compiled, 1.19);
    auto boundaries = equal_boundaries(compiled, 210000.0, 600.0);
    auto workspace = make_one_level_master_rod_coupled_reaction_workspace(compiled);

    (void)require_stage(stage_one_level_master_rod_piston_wall_friction(
        compiled, state, boundaries, 123.0, workspace));
    (void)require_reaction(calculate_one_level_master_rod_coupled_reactions(
        compiled, state, 123.0, 7.0, workspace));

    boundaries.back().chamber_pressure_pa_abs =
        std::numeric_limits<double>::quiet_NaN();
    expect_stage_issue(stage_one_level_master_rod_piston_wall_friction(
                           compiled, state, boundaries, 123.0, workspace),
                       OneLevelMasterRodCoupledReactionIssue::nonfinite_boundary_value,
                       "late-cylinder staging failure was not reported");
    expect_reaction_issue(
        calculate_one_level_master_rod_coupled_reactions(compiled, state, 123.0, 7.0,
                                                         workspace),
        OneLevelMasterRodCoupledReactionIssue::missing_friction_stage,
        "late-cylinder staging failure left a reusable hybrid transaction");
}

void test_public_radial_five_reaction_traversal() {
    auto compiled = require_compiled(
        compile_one_level_master_rod_articulated_mechanism(radial_five_plan()));
    auto workspace = make_one_level_master_rod_coupled_reaction_workspace(compiled);
    constexpr std::array theta_samples{0.17, 1.41, 3.87, 5.92};

    for (std::size_t sample = 0; sample < theta_samples.size(); ++sample) {
        auto state = state_at(compiled, theta_samples[sample]);
        auto boundaries = equal_boundaries(compiled, 101325.0, 250.0);
        for (std::size_t cylinder = 0; cylinder < boundaries.size(); ++cylinder) {
            boundaries[cylinder].chamber_pressure_pa_abs +=
                25000.0 * static_cast<double>(1U + sample + cylinder);
        }
        const double omega = 80.0 + 37.0 * static_cast<double>(sample);
        const double alpha = -45.0 + 19.0 * static_cast<double>(sample);
        const auto staged =
            require_stage(stage_one_level_master_rod_piston_wall_friction(
                compiled, state, boundaries, omega, workspace));
        expect(staged.cylinders.size() == compiled.cylinder_count(),
               "radial-five public stage omitted a cylinder");
        const auto resolved =
            require_reaction(calculate_one_level_master_rod_coupled_reactions(
                compiled, state, omega, alpha, workspace));
        expect(resolved.cylinders.size() == compiled.cylinder_count(),
               "radial-five public reaction omitted a cylinder");
        for (const auto &reaction : resolved.cylinders) {
            expect(std::isfinite(reaction.signed_wall_on_piston_force_n) &&
                       std::isfinite(reaction.wall_reaction_magnitude_n),
                   "radial-five public reaction was nonfinite");
        }
    }
}

} // namespace

int main() {
    try {
        test_direct_root_reduces_to_existing_centered_reaction();
        test_slave_force_propagates_to_root_and_order_is_irrelevant();
        test_loaded_root_slave_satisfies_independent_newton_euler_balances();
        test_radial_five_matches_independent_ideal_wall_oracle();
        test_friction_is_dissipative_causal_and_malformed_inputs_fail_closed();
        test_workspace_transactions_are_mechanism_and_state_bound();
        test_failed_late_stage_invalidates_the_whole_transaction();
        test_public_radial_five_reaction_traversal();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
