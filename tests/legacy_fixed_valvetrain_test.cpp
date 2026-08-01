#include "contract_test_support.hpp"
#include "simulation/legacy_fixed_valvetrain.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;
using namespace engine_sim_offline::simulation;

constexpr double kDegreeSource = kLegacyPi / 180.0;
constexpr double kMaximumLiftM = 9.0 * (1.0 / 1000.0);
constexpr double kExpectedLobeExtentRad = 1.1567066159922268;
constexpr double kExpectedLobeRadiusRad = 0.01217585911570765;
constexpr double kExpectedLobeDomainRad = 1.2054100524550575;
constexpr double kExpectedLiftAt105CrankDegreesM = 0.0014591061116522217;
constexpr double kExpectedPeakIntakeK = 0.00632193692425645;
constexpr double kExpectedPeakExhaustK = 0.004397869164700139;
constexpr double kExpectedIntakeKAt105CrankDegrees = 0.0012775164753657624;
constexpr double kExpectedExhaustKAt105CrankDegrees = 0.001214419956248241;

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] LowOrderOperatingPointV1Profile &operating_profile(EngineSpec &engine) {
    auto *profile =
        std::get_if<LowOrderOperatingPointV1Profile>(&engine.physics_profile);
    expect(profile != nullptr, "test engine lost its operating-point profile");
    return *profile;
}

[[nodiscard]] LegacyCamShape &only_cam_profile(LegacyCamshaftProfile &camshaft) {
    expect(camshaft.profiles.size() == 1U,
           "fixture camshaft did not contain exactly one profile");
    return camshaft.profiles.front();
}

[[nodiscard]] LegacyFixedValvetrainCompileResult
compile_fixture_valvetrain(EngineSpec &engine) {
    return compile_legacy_fixed_valvetrain(engine, operating_profile(engine).core);
}

[[nodiscard]] double resolved_flow_k(double source_cfm) {
    constexpr double gamma = 1.4;
    constexpr double gas_constant = 8.31446261815324;
    constexpr double pressure_pa = 101325.0;
    constexpr double temperature_k = 298.15;
    const double one_source_scfm = 0.002641 * 453.59237 / 60.0;
    const double pressure_drop_pa = 28.0 * (3386.3886666666713 * 0.0734824);
    const double pressure_target_pa = pressure_pa - pressure_drop_pa;
    const double ratio = pressure_target_pa / pressure_pa;
    const double critical = std::pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
    double flow = 0.0;
    if (ratio <= critical) {
        flow = std::sqrt(gamma);
        flow *= std::pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
    } else {
        flow = (2.0 * gamma) / (gamma - 1.0);
        flow *= 1.0 - std::pow(ratio, (gamma - 1.0) / gamma);
        flow = std::sqrt(flow);
        flow *= std::pow(ratio, 1.0 / gamma);
    }
    flow *= pressure_pa / std::sqrt(gas_constant * temperature_k);
    return source_cfm * one_source_scfm / flow;
}

[[nodiscard]] std::vector<LegacyValveFlowPoint>
make_flow_table(const LegacyValveFlowPoint &prototype,
                const std::array<double, 13> &source_cfm) {
    std::vector<LegacyValveFlowPoint> table;
    table.reserve(source_cfm.size());
    for (std::size_t index = 0; index < source_cfm.size(); ++index) {
        auto point = prototype;
        point.sample_id.value = "lift-" + std::to_string(index);
        point.lift_m.value = static_cast<double>(index) * 0.001;
        point.source_cfm_at_28_inh2o.value = source_cfm[index];
        point.resolved_k.value = resolved_flow_k(source_cfm[index]);
        table.push_back(std::move(point));
    }
    return table;
}

struct ValvetrainFixture {
    InputBuilder builder;
    EngineSpec engine;

    ValvetrainFixture() : engine(make_engine(builder)) {
        auto &profile = operating_profile(engine);
        auto configure_shape = [](LegacyCamShape &shape) {
            auto &harmonic = std::get<LegacyHarmonicCamShape>(shape);
            const double centimetre_source = 1.0 / 100.0;
            const double inch_source = centimetre_source * 2.54;
            harmonic.maximum_lift_m.value = kMaximumLiftM;
            harmonic.duration_at_reference_lift_rad.value = 210.0 * kDegreeSource;
            harmonic.exponent.value = 0.8;
            harmonic.construction_steps.value = 100;
            harmonic.advance_rad.value = 0.0;
            harmonic.base_radius_m.value = 0.6 * inch_source;
        };
        configure_shape(only_cam_profile(profile.core.valvetrain.intake));
        configure_shape(only_cam_profile(profile.core.valvetrain.exhaust));

        constexpr std::array<double, 13> intake_cfm{
            0.0,   35.0,  60.0,  90.0,  125.0, 150.0, 175.0,
            200.0, 215.0, 230.0, 235.0, 235.0, 238.0,
        };
        constexpr std::array<double, 13> exhaust_cfm{
            0.0,   35.0,  55.0,  85.0,  105.0, 120.0, 140.0,
            150.0, 155.0, 160.0, 165.0, 165.0, 165.0,
        };
        auto &head = profile.core.gas_path.heads.front();
        head.intake_flow = make_flow_table(head.intake_flow.front(), intake_cfm);
        head.exhaust_flow = make_flow_table(head.exhaust_flow.front(), exhaust_cfm);
        head.intake_flow_triangle_radius_m.value = 0.001;
        head.exhaust_flow_triangle_radius_m.value = 0.001;

        auto second_cylinder = engine.cylinders.front();
        second_cylinder.id = CylinderId{2};
        second_cylinder.semantic_id.value = "cylinder-2";
        engine.cylinders.push_back(std::move(second_cylinder));

        auto second_intake_port = engine.ports[0];
        second_intake_port.id = PortId{3};
        second_intake_port.semantic_id.value = "intake-port-2";
        second_intake_port.cylinder_id = CylinderId{2};
        auto second_exhaust_port = engine.ports[1];
        second_exhaust_port.id = PortId{4};
        second_exhaust_port.semantic_id.value = "exhaust-port-2";
        second_exhaust_port.cylinder_id = CylinderId{2};
        engine.ports.push_back(std::move(second_intake_port));
        engine.ports.push_back(std::move(second_exhaust_port));

        auto second_mechanism = profile.core.mechanism.cylinders.front();
        second_mechanism.topology.cylinder_id = CylinderId{2};
        second_mechanism.topology.intake_port_id = PortId{3};
        second_mechanism.topology.exhaust_port_id = PortId{4};
        profile.core.mechanism.cylinders.push_back(std::move(second_mechanism));

        profile.core.valvetrain.intake.lobes.front().crank_center_rad.value = 0.0;
        profile.core.valvetrain.exhaust.lobes.front().crank_center_rad.value = 0.0;
        auto second_intake_lobe = profile.core.valvetrain.intake.lobes.front();
        second_intake_lobe.cylinder_id = CylinderId{2};
        second_intake_lobe.port_id = PortId{3};
        second_intake_lobe.crank_center_rad.value = 710.0 * kDegreeSource;
        auto second_exhaust_lobe = profile.core.valvetrain.exhaust.lobes.front();
        second_exhaust_lobe.cylinder_id = CylinderId{2};
        second_exhaust_lobe.port_id = PortId{4};
        second_exhaust_lobe.crank_center_rad.value = 855.0 * kDegreeSource;
        profile.core.valvetrain.intake.lobes.push_back(std::move(second_intake_lobe));
        profile.core.valvetrain.exhaust.lobes.push_back(std::move(second_exhaust_lobe));

        // Admission must resolve by stable cylinder identity, not lobe-vector order.
        std::reverse(profile.core.valvetrain.intake.lobes.begin(),
                     profile.core.valvetrain.intake.lobes.end());
        std::reverse(profile.core.valvetrain.exhaust.lobes.begin(),
                     profile.core.valvetrain.exhaust.lobes.end());
    }
};

[[nodiscard]] LegacySampledCamShape
make_sampled_shape(InputBuilder &builder, std::string role, double advance_rad = 0.5) {
    const std::string base = "engine.physics.low-order-operating-point-v1.valvetrain." +
                             role + ".profiles.profile-0.shape";
    const auto make_point = [&](std::string id, double angle_rad, double lift_m) {
        const std::string point_base = base + ".samples." + id;
        LegacySampledCamPoint point;
        point.sample_id = builder.resolved(std::move(id), point_base + ".sample_id");
        point.angle_rad = builder.resolved(angle_rad, point_base + ".angle_rad");
        point.lift_m = builder.resolved(lift_m, point_base + ".lift_m");
        return point;
    };

    LegacySampledCamShape shape;
    shape.triangle_radius_rad = builder.resolved(1.0, base + ".triangle_radius_rad");
    shape.samples = {
        make_point(role + "-left", -1.0, 0.001),
        make_point(role + "-center", 0.0, 0.009),
        make_point(role + "-right", 1.0, 0.002),
    };
    shape.advance_rad = builder.resolved(advance_rad, base + ".advance_rad");
    shape.base_radius_m = builder.resolved(0.015, base + ".base_radius_m");
    return shape;
}

void configure_sampled_shapes(ValvetrainFixture &fixture) {
    auto &valvetrain = operating_profile(fixture.engine).core.valvetrain;
    only_cam_profile(valvetrain.intake) = make_sampled_shape(fixture.builder, "intake");
    only_cam_profile(valvetrain.exhaust) =
        make_sampled_shape(fixture.builder, "exhaust");
}

[[nodiscard]] LegacyHarmonicCamShape &harmonic_shape(LegacyCamShape &shape) {
    return std::get<LegacyHarmonicCamShape>(shape);
}

[[nodiscard]] LegacySampledCamShape &sampled_shape(LegacyCamShape &shape) {
    return std::get<LegacySampledCamShape>(shape);
}

[[nodiscard]] LegacyFixedValvetrain
require_valvetrain(LegacyFixedValvetrainCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        std::string message = "valid fixed valvetrain was rejected";
        if (!report->issues.empty()) {
            message += ": " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<LegacyFixedValvetrain>(std::move(result));
}

template <class Mutation>
void expect_compile_rejected(Mutation mutation, std::string_view expected_path) {
    ValvetrainFixture fixture;
    mutation(fixture.engine, operating_profile(fixture.engine));
    auto result = compile_fixture_valvetrain(fixture.engine);
    const auto *report = std::get_if<ValidationReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{
            "invalid fixed valvetrain compiled successfully; expected issue at " +
            std::string{expected_path}};
    }
    const bool found = std::any_of(
        report->issues.begin(), report->issues.end(), [&](const auto &issue) {
            return issue.path.find(expected_path) != std::string::npos;
        });
    if (!found) {
        throw std::runtime_error{
            "valvetrain rejection omitted the expected issue path " +
            std::string{expected_path}};
    }
}

template <class Mutation>
void expect_sampled_compile_rejected(Mutation mutation,
                                     std::string_view expected_path) {
    ValvetrainFixture fixture;
    configure_sampled_shapes(fixture);
    mutation(fixture.engine, operating_profile(fixture.engine));
    auto result = compile_fixture_valvetrain(fixture.engine);
    const auto *report = std::get_if<ValidationReport>(&result);
    if (report == nullptr) {
        throw std::runtime_error{
            "invalid sampled fixed valvetrain compiled successfully; expected issue "
            "at " +
            std::string{expected_path}};
    }
    const bool found = std::any_of(
        report->issues.begin(), report->issues.end(), [&](const auto &issue) {
            return issue.path.find(expected_path) != std::string::npos;
        });
    if (!found) {
        throw std::runtime_error{
            "sampled valvetrain rejection omitted the expected issue path " +
            std::string{expected_path}};
    }
}

void test_exact_lobe_construction_and_bindings() {
    ValvetrainFixture fixture;
    const auto &profile = operating_profile(fixture.engine);
    auto valvetrain = require_valvetrain(compile_fixture_valvetrain(fixture.engine));

    const auto intake_profiles = valvetrain.intake_cam_profiles();
    const auto exhaust_profiles = valvetrain.exhaust_cam_profiles();
    expect(intake_profiles.size() == 1U && exhaust_profiles.size() == 1U,
           "single-profile fixture did not compile one profile per valve role");
    const auto &intake_profile = intake_profiles.front();
    const auto &exhaust_profile = exhaust_profiles.front();
    const auto &intake = intake_profile.lobe_table;
    const auto &exhaust = exhaust_profile.lobe_table;
    expect(intake.size() == 199 && exhaust.size() == 199,
           "N=100 did not produce two 199-sample lobe tables");
    expect(same_binary64(intake_profile.lobe_triangle_radius_rad,
                         kExpectedLobeRadiusRad) &&
               same_binary64(exhaust_profile.lobe_triangle_radius_rad,
                             kExpectedLobeRadiusRad),
           "BMW lobe triangle radius changed");
    expect(same_binary64(intake.front().x, -kExpectedLobeDomainRad) &&
               same_binary64(intake.back().x, kExpectedLobeDomainRad) &&
               same_binary64(exhaust.front().x, -kExpectedLobeDomainRad) &&
               same_binary64(exhaust.back().x, kExpectedLobeDomainRad),
           "BMW lobe padding domain changed");
    expect(same_binary64(intake[99].x, 0.0) &&
               same_binary64(intake[99].y, kMaximumLiftM) &&
               same_binary64(exhaust[99].x, 0.0) &&
               same_binary64(exhaust[99].y, kMaximumLiftM),
           "BMW lobe center sample changed");

    for (std::size_t index = 0; index < intake.size(); ++index) {
        const std::size_t mirror = intake.size() - 1U - index;
        expect(index == 0 || intake[index - 1U].x < intake[index].x,
               "intake lobe table is not strictly ordered");
        expect(index == 0 || exhaust[index - 1U].x < exhaust[index].x,
               "exhaust lobe table is not strictly ordered");
        const bool intake_x_symmetric =
            index == 99U ? intake[index].x == 0.0
                         : same_binary64(intake[index].x, -intake[mirror].x);
        const bool exhaust_x_symmetric =
            index == 99U ? exhaust[index].x == 0.0
                         : same_binary64(exhaust[index].x, -exhaust[mirror].x);
        expect(intake_x_symmetric && same_binary64(intake[index].y, intake[mirror].y) &&
                   exhaust_x_symmetric &&
                   same_binary64(exhaust[index].y, exhaust[mirror].y),
               "lobe table lost exact signed symmetry");
    }
    for (std::size_t index = 0; index < 5; ++index) {
        expect(intake[index].y == 0.0 && exhaust[index].y == 0.0 &&
                   intake[198U - index].y == 0.0 && exhaust[198U - index].y == 0.0,
               "lobe table did not retain all five zero-tail pairs");
    }
    expect(intake[5].y > 0.0 && intake[193].y > 0.0 && exhaust[5].y > 0.0 &&
               exhaust[193].y > 0.0,
           "lobe table zero tail grew beyond the five admitted pairs");
    expect(same_binary64(std::abs(intake[4].x), kExpectedLobeExtentRad) &&
               same_binary64(std::abs(intake[194].x), kExpectedLobeExtentRad),
           "zero-at-extent lobe samples moved");

    const auto bindings = valvetrain.cylinder_bindings();
    expect(bindings.size() == 2 && bindings[0].cylinder_id == CylinderId{1} &&
               bindings[0].intake_port_id == PortId{1} &&
               bindings[0].exhaust_port_id == PortId{2} &&
               bindings[0].flow_profile_index == 0U &&
               bindings[0].intake_cam_profile_index == 0U &&
               bindings[0].exhaust_cam_profile_index == 0U &&
               bindings[1].cylinder_id == CylinderId{2} &&
               bindings[1].intake_port_id == PortId{3} &&
               bindings[1].exhaust_port_id == PortId{4} &&
               bindings[1].flow_profile_index == 0U &&
               bindings[1].intake_cam_profile_index == 0U &&
               bindings[1].exhaust_cam_profile_index == 0U,
           "lobe-vector reordering changed admitted cylinder/port order");
    const auto intake_lobe_2 =
        std::find_if(profile.core.valvetrain.intake.lobes.begin(),
                     profile.core.valvetrain.intake.lobes.end(), [](const auto &lobe) {
                         return lobe.cylinder_id == CylinderId{2};
                     });
    const auto exhaust_lobe_2 =
        std::find_if(profile.core.valvetrain.exhaust.lobes.begin(),
                     profile.core.valvetrain.exhaust.lobes.end(), [](const auto &lobe) {
                         return lobe.cylinder_id == CylinderId{2};
                     });
    expect(intake_lobe_2 != profile.core.valvetrain.intake.lobes.end() &&
               exhaust_lobe_2 != profile.core.valvetrain.exhaust.lobes.end() &&
               same_binary64(bindings[1].intake_stored_lobe_angle_rad,
                             intake_lobe_2->crank_center_rad.value / 2.0) &&
               same_binary64(bindings[1].exhaust_stored_lobe_angle_rad,
                             exhaust_lobe_2->crank_center_rad.value / 2.0),
           "raw crank centers were wrapped or reordered before half-speed storage");
}

void test_sampling_goldens_wrap_and_span_contract() {
    ValvetrainFixture fixture;
    auto valvetrain = require_valvetrain(compile_fixture_valvetrain(fixture.engine));

    const auto peak = valvetrain.sample_cylinder(std::size_t{0}, 0.0);
    expect(peak.has_value() && peak->cylinder_id == CylinderId{1} &&
               peak->intake_port_id == PortId{1} && peak->exhaust_port_id == PortId{2},
           "peak sample lost its stable identities");
    expect(same_binary64(peak->intake_lobe_argument_rad, 0.0) &&
               same_binary64(peak->exhaust_lobe_argument_rad, 0.0) &&
               same_binary64(peak->intake_lift_m, kMaximumLiftM) &&
               same_binary64(peak->exhaust_lift_m, kMaximumLiftM),
           "zero-phase lobe sample is not the exact lift peak");
    expect(same_binary64(peak->intake_valve_k, kExpectedPeakIntakeK) &&
               same_binary64(peak->exhaust_valve_k, kExpectedPeakExhaustK) &&
               peak->intake_valve_k != peak->exhaust_valve_k,
           "peak valve conductance lost the distinct BMW flow tables");
    expect(peak->intake_lift_m <
                   valvetrain.intake_cam_profiles().front().base_radius_m &&
               peak->exhaust_lift_m <
                   valvetrain.exhaust_cam_profiles().front().base_radius_m,
           "cam base radius was added to valve lift");

    const double crank105 = 105.0 * kDegreeSource;
    const auto positive105 = valvetrain.sample_cylinder(CylinderId{1}, crank105);
    const auto negative105 = valvetrain.sample_cylinder(CylinderId{1}, -crank105);
    expect(positive105.has_value() && negative105.has_value(),
           "valid +/-105-degree valve samples were rejected");
    expect(same_binary64(positive105->intake_lift_m, kExpectedLiftAt105CrankDegreesM) &&
               same_binary64(positive105->exhaust_lift_m,
                             kExpectedLiftAt105CrankDegreesM) &&
               same_binary64(positive105->intake_valve_k,
                             kExpectedIntakeKAt105CrankDegrees) &&
               same_binary64(positive105->exhaust_valve_k,
                             kExpectedExhaustKAt105CrankDegrees),
           "discrete +105-degree sample changed or became analytic");
    // The negative path reaches the symmetric table through a separately rounded
    // wrapped argument, so source arithmetic differs by a few ULPs rather than
    // manufacturing exact bit symmetry.
    expect(std::isfinite(negative105->intake_lift_m) &&
               std::isfinite(negative105->exhaust_lift_m) &&
               std::abs(negative105->intake_lift_m - kExpectedLiftAt105CrankDegreesM) <=
                   2.0e-18 &&
               std::abs(negative105->exhaust_lift_m -
                        kExpectedLiftAt105CrankDegreesM) <= 2.0e-18 &&
               std::abs(negative105->intake_valve_k -
                        kExpectedIntakeKAt105CrankDegrees) <= 2.0e-18 &&
               std::abs(negative105->exhaust_valve_k -
                        kExpectedExhaustKAt105CrankDegrees) <= 2.0e-18,
           "discrete -105-degree source-rounded sample changed");

    const auto closed =
        valvetrain.sample_cylinder(CylinderId{1}, 180.0 * kDegreeSource);
    expect(closed.has_value() && closed->intake_lift_m == 0.0 &&
               closed->exhaust_lift_m == 0.0 && closed->intake_valve_k == 0.0 &&
               closed->exhaust_valve_k == 0.0,
           "closed-zone cam sample did not clamp to zero");

    const auto positive_pi = valvetrain.sample_cylinder(CylinderId{1}, 2.0 * kLegacyPi);
    const auto negative_pi =
        valvetrain.sample_cylinder(CylinderId{1}, -2.0 * kLegacyPi);
    expect(positive_pi.has_value() && negative_pi.has_value() &&
               same_binary64(positive_pi->intake_lobe_argument_rad, -kLegacyPi) &&
               same_binary64(negative_pi->intake_lobe_argument_rad, -kLegacyPi),
           "cam +pi boundary no longer wraps to -pi");

    std::array<LegacyCylinderValveSample, 2> output{};
    expect(valvetrain.sample_all(0.0, output),
           "exact caller-owned output span was rejected");
    expect(output[0] == *peak && output[1].cylinder_id == CylinderId{2},
           "sample_all changed engine cylinder order or per-cylinder values");
    std::array<LegacyCylinderValveSample, 3> oversized{};
    expect(!valvetrain.sample_all(0.0, std::span{oversized}) &&
               !valvetrain.sample_all(0.0, std::span<LegacyCylinderValveSample>{}) &&
               !valvetrain.sample_all(std::numeric_limits<double>::quiet_NaN(), output),
           "allocation-free span sampler accepted an invalid extent or angle");
    expect(!valvetrain.sample_cylinder(std::size_t{2}, 0.0).has_value() &&
               !valvetrain.sample_cylinder(CylinderId{99}, 0.0).has_value() &&
               !valvetrain
                    .sample_cylinder(CylinderId{1},
                                     std::numeric_limits<double>::infinity())
                    .has_value(),
           "single-cylinder sampler accepted an invalid identity, index, or angle");
}

void test_sampled_lobe_copy_endpoints_wrap_advance_and_multiple_lobes() {
    ValvetrainFixture fixture;
    configure_sampled_shapes(fixture);
    auto &profile = operating_profile(fixture.engine);

    auto &second_intake_lobe =
        *std::find_if(profile.core.valvetrain.intake.lobes.begin(),
                      profile.core.valvetrain.intake.lobes.end(), [](const auto &lobe) {
                          return lobe.cylinder_id == CylinderId{2};
                      });
    auto &second_exhaust_lobe = *std::find_if(
        profile.core.valvetrain.exhaust.lobes.begin(),
        profile.core.valvetrain.exhaust.lobes.end(),
        [](const auto &lobe) { return lobe.cylinder_id == CylinderId{2}; });
    second_intake_lobe.crank_center_rad.value = 2.0;
    second_exhaust_lobe.crank_center_rad.value = -2.0;

    auto valvetrain = require_valvetrain(compile_fixture_valvetrain(fixture.engine));
    const std::array<LegacyTrianglePoint, 3> expected_table{{
        {-1.0, 0.001},
        {0.0, 0.009},
        {1.0, 0.002},
    }};
    const auto &intake_profile = valvetrain.intake_cam_profiles().front();
    const auto &exhaust_profile = valvetrain.exhaust_cam_profiles().front();
    expect(std::ranges::equal(intake_profile.lobe_table, expected_table) &&
               std::ranges::equal(exhaust_profile.lobe_table, expected_table),
           "sampled cam points were reordered, regenerated, or otherwise changed");
    expect(same_binary64(intake_profile.lobe_triangle_radius_rad, 1.0) &&
               same_binary64(exhaust_profile.lobe_triangle_radius_rad, 1.0) &&
               same_binary64(intake_profile.advance_rad, 0.5) &&
               same_binary64(exhaust_profile.advance_rad, 0.5) &&
               same_binary64(intake_profile.base_radius_m, 0.015) &&
               same_binary64(exhaust_profile.base_radius_m, 0.015),
           "sampled cam metadata changed during fixed-valvetrain compilation");

    const auto advanced_peak = valvetrain.sample_cylinder(CylinderId{1}, -0.5);
    const auto unshifted_body_angle = valvetrain.sample_cylinder(CylinderId{1}, 0.0);
    expect(advanced_peak.has_value() && unshifted_body_angle.has_value() &&
               same_binary64(advanced_peak->intake_lobe_argument_rad, 0.0) &&
               same_binary64(advanced_peak->exhaust_lobe_argument_rad, 0.0) &&
               same_binary64(advanced_peak->intake_lift_m, 0.009) &&
               same_binary64(advanced_peak->exhaust_lift_m, 0.009) &&
               unshifted_body_angle->intake_lift_m < advanced_peak->intake_lift_m,
           "nonzero sampled-cam advance did not shift the exact profile peak");

    const auto left_endpoint = valvetrain.sample_cylinder(CylinderId{1}, -2.5);
    const auto right_endpoint = valvetrain.sample_cylinder(CylinderId{1}, 1.5);
    const auto left_clamp = valvetrain.sample_cylinder(CylinderId{1}, -4.5);
    const auto right_clamp = valvetrain.sample_cylinder(CylinderId{1}, 3.5);
    expect(left_endpoint.has_value() && right_endpoint.has_value() &&
               left_clamp.has_value() && right_clamp.has_value() &&
               std::abs(left_endpoint->intake_lift_m - 0.001) <= 1.0e-18 &&
               same_binary64(right_endpoint->intake_lift_m, 0.002) &&
               same_binary64(left_clamp->intake_lift_m, 0.001) &&
               same_binary64(right_clamp->intake_lift_m, 0.002),
           "sampled cam endpoint or out-of-domain clamp semantics changed");

    const auto positive_wrap =
        valvetrain.sample_cylinder(CylinderId{1}, 2.0 * kLegacyPi - 0.5);
    const auto negative_wrap =
        valvetrain.sample_cylinder(CylinderId{1}, -2.0 * kLegacyPi - 0.5);
    expect(positive_wrap.has_value() && negative_wrap.has_value() &&
               same_binary64(positive_wrap->intake_lobe_argument_rad, -kLegacyPi) &&
               same_binary64(negative_wrap->intake_lobe_argument_rad, -kLegacyPi) &&
               same_binary64(positive_wrap->intake_lift_m, 0.001) &&
               same_binary64(negative_wrap->intake_lift_m, 0.001),
           "sampled cam did not retain the source +pi-to-minus-pi wrap and clamp");

    std::array<LegacyCylinderValveSample, 2> output{};
    expect(valvetrain.sample_all(-0.5, output) &&
               same_binary64(output[0].intake_lift_m, 0.009) &&
               same_binary64(output[0].exhaust_lift_m, 0.009) &&
               same_binary64(output[1].intake_lobe_argument_rad, 1.0) &&
               same_binary64(output[1].exhaust_lobe_argument_rad, -1.0) &&
               same_binary64(output[1].intake_lift_m, 0.002) &&
               std::abs(output[1].exhaust_lift_m - 0.001) <= 1.0e-18,
           "sampled cam profile was not shared across independently phased lobes");
}

void test_bank_local_cam_profiles_sample_by_cylinder_binding() {
    ValvetrainFixture fixture;
    auto &profile = operating_profile(fixture.engine);
    auto &intake = profile.core.valvetrain.intake;
    auto &exhaust = profile.core.valvetrain.exhaust;
    intake.profiles.push_back(intake.profiles.front());
    exhaust.profiles.push_back(exhaust.profiles.front());
    harmonic_shape(intake.profiles[1]).maximum_lift_m.value = 0.012;
    harmonic_shape(intake.profiles[1]).advance_rad.value = 0.31;
    harmonic_shape(exhaust.profiles[1]).maximum_lift_m.value = 0.011;
    harmonic_shape(exhaust.profiles[1]).advance_rad.value = -0.23;
    for (auto &lobe : intake.lobes) {
        lobe.profile_index = lobe.cylinder_id == CylinderId{2} ? 1U : 0U;
    }
    for (auto &lobe : exhaust.lobes) {
        lobe.profile_index = lobe.cylinder_id == CylinderId{2} ? 1U : 0U;
    }

    const auto valvetrain =
        require_valvetrain(compile_fixture_valvetrain(fixture.engine));
    expect(valvetrain.intake_cam_profiles().size() == 2U &&
               valvetrain.exhaust_cam_profiles().size() == 2U,
           "bank-local cam fixture did not compile both profile pools");
    const auto binding_for = [&](const CylinderId cylinder_id) {
        const auto binding =
            std::ranges::find(valvetrain.cylinder_bindings(), cylinder_id,
                              &LegacyValvetrainCylinderBinding::cylinder_id);
        expect(binding != valvetrain.cylinder_bindings().end(),
               "bank-local cam fixture lost a cylinder binding");
        return *binding;
    };
    const auto first_binding = binding_for(CylinderId{1});
    const auto second_binding = binding_for(CylinderId{2});
    expect(first_binding.intake_cam_profile_index == 0U &&
               first_binding.exhaust_cam_profile_index == 0U &&
               second_binding.intake_cam_profile_index == 1U &&
               second_binding.exhaust_cam_profile_index == 1U,
           "bank-local cam profile indices collapsed during compilation");

    const auto *output_crank = find_output_crank(profile.core.mechanism);
    expect(output_crank != nullptr, "bank-local cam fixture lost its output crank");
    const auto peak_lift = [&](const LegacyValvetrainCylinderBinding &binding,
                               const bool intake_role) {
        const auto &cam =
            intake_role
                ? valvetrain.intake_cam_profiles()[binding.intake_cam_profile_index]
                : valvetrain.exhaust_cam_profiles()[binding.exhaust_cam_profile_index];
        const double stored_angle = intake_role ? binding.intake_stored_lobe_angle_rad
                                                : binding.exhaust_stored_lobe_angle_rad;
        const double body_angle = output_crank->crank_tdc_reference_rad.value -
                                  cam.advance_rad - 2.0 * stored_angle;
        const auto sample = valvetrain.sample_cylinder(binding.cylinder_id, body_angle);
        expect(sample.has_value(),
               "bank-local cam fixture rejected a finite peak sample");
        return intake_role ? sample->intake_lift_m : sample->exhaust_lift_m;
    };
    expect(same_binary64(peak_lift(first_binding, true), kMaximumLiftM) &&
               same_binary64(peak_lift(first_binding, false), kMaximumLiftM) &&
               same_binary64(peak_lift(second_binding, true), 0.012) &&
               same_binary64(peak_lift(second_binding, false), 0.011),
           "cylinder sampling did not use its bound bank-local cam profile");
}

void test_bank_local_flow_profiles_bind_by_stable_bank_identity() {
    ValvetrainFixture fixture;
    auto &profile = operating_profile(fixture.engine);

    auto second_bank = fixture.engine.banks.front();
    second_bank.id = BankId{2};
    second_bank.semantic_id.value = "bank-2";
    fixture.engine.banks.push_back(std::move(second_bank));
    fixture.engine.cylinders[0].bank_id = BankId{2};
    fixture.engine.cylinders[1].bank_id = BankId{1};

    auto second_head = profile.core.gas_path.heads.front();
    second_head.bank_id = BankId{2};
    second_head.intake_flow_triangle_radius_m.value = 0.0005;
    second_head.exhaust_flow_triangle_radius_m.value = 0.0015;
    for (auto &point : second_head.intake_flow) {
        point.resolved_k.value *= 0.5;
    }
    for (auto &point : second_head.exhaust_flow) {
        point.resolved_k.value *= 0.25;
    }
    profile.core.gas_path.heads.push_back(std::move(second_head));

    for (auto &lobe : profile.core.valvetrain.intake.lobes) {
        lobe.crank_center_rad.value = 0.0;
    }
    for (auto &lobe : profile.core.valvetrain.exhaust.lobes) {
        lobe.crank_center_rad.value = 0.0;
    }

    auto valvetrain = require_valvetrain(compile_fixture_valvetrain(fixture.engine));
    const auto flows = valvetrain.flow_profiles();
    const auto bindings = valvetrain.cylinder_bindings();
    expect(flows.size() == 2U && flows[0].bank_id == BankId{1} &&
               flows[1].bank_id == BankId{2} && bindings.size() == 2U &&
               bindings[0].flow_profile_index == 1U &&
               bindings[1].flow_profile_index == 0U,
           "crossed cylinder bank assignments changed stable flow-profile binding");
    expect(same_binary64(flows[0].intake_flow_triangle_radius_m, 0.001) &&
               same_binary64(flows[0].exhaust_flow_triangle_radius_m, 0.001) &&
               same_binary64(flows[1].intake_flow_triangle_radius_m, 0.0005) &&
               same_binary64(flows[1].exhaust_flow_triangle_radius_m, 0.0015),
           "bank-local intake/exhaust flow radii changed during compilation");

    const double crank105 = 105.0 * kDegreeSource;
    const auto first = valvetrain.sample_cylinder(CylinderId{1}, crank105);
    const auto second = valvetrain.sample_cylinder(CylinderId{2}, crank105);
    expect(first.has_value() && second.has_value() &&
               same_binary64(first->intake_valve_k,
                             legacy_triangle_sample(
                                 flows[1].intake_flow_table, first->intake_lift_m,
                                 flows[1].intake_flow_triangle_radius_m)) &&
               same_binary64(first->exhaust_valve_k,
                             legacy_triangle_sample(
                                 flows[1].exhaust_flow_table, first->exhaust_lift_m,
                                 flows[1].exhaust_flow_triangle_radius_m)) &&
               second->intake_valve_k != first->intake_valve_k &&
               second->exhaust_valve_k != first->exhaust_valve_k,
           "cylinder sampling did not use its explicitly bound bank-local flow "
           "profile");
    expect(!same_binary64(
               first->intake_valve_k,
               legacy_triangle_sample(flows[1].intake_flow_table, first->intake_lift_m,
                                      flows[1].exhaust_flow_triangle_radius_m)) &&
               !same_binary64(first->exhaust_valve_k,
                              legacy_triangle_sample(
                                  flows[1].exhaust_flow_table, first->exhaust_lift_m,
                                  flows[1].intake_flow_triangle_radius_m)),
           "bank-local intake/exhaust flow radii collapsed back to one radius");
}

void test_focused_admission_failures() {
    expect_compile_rejected(
        [](EngineSpec &engine, LowOrderOperatingPointV1Profile &) {
            engine.methods.valvetrain.value.id = "unsupported-valvetrain";
        },
        "engine.methods.valvetrain");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.valvetrain.intake.lobes[0].cylinder_id =
                profile.core.valvetrain.intake.lobes[1].cylinder_id;
        },
        "valvetrain.intake.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.valvetrain.exhaust.lobes.pop_back();
        },
        "valvetrain.exhaust.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.valvetrain.intake.profiles.clear();
        },
        "valvetrain.intake.profiles");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.valvetrain.intake.lobes.front().profile_index = 1U;
        },
        "valvetrain.intake.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.valvetrain.intake.profiles.push_back(
                profile.core.valvetrain.intake.profiles.front());
        },
        "valvetrain.intake.profiles");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            auto &camshaft = profile.core.valvetrain.intake;
            camshaft.profiles.push_back(camshaft.profiles.front());
            for (auto &lobe : camshaft.lobes) {
                lobe.profile_index = lobe.cylinder_id == CylinderId{1} ? 1U : 0U;
            }
        },
        "valvetrain.intake.profiles");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            auto &cylinder_one = *std::find_if(
                profile.core.valvetrain.intake.lobes.begin(),
                profile.core.valvetrain.intake.lobes.end(),
                [](const auto &lobe) { return lobe.cylinder_id == CylinderId{1}; });
            cylinder_one.port_id = PortId{2};
        },
        "valvetrain.intake.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.gas_path.heads.front().intake_flow[1].lift_m.value =
                profile.core.gas_path.heads.front().intake_flow[0].lift_m.value;
        },
        "gas_path.heads[0].intake_flow");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.gas_path.heads.front().exhaust_flow.resize(1);
        },
        "gas_path.heads[0].exhaust_flow");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.gas_path.heads.front().intake_flow_triangle_radius_m.value =
                0.0;
        },
        "gas_path.heads[0].intake_flow_triangle_radius_m");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            profile.core.gas_path.heads.clear();
        },
        "gas_path.heads");
    expect_compile_rejected(
        [](EngineSpec &engine, LowOrderOperatingPointV1Profile &profile) {
            auto second_bank = engine.banks.front();
            second_bank.id = BankId{2};
            second_bank.semantic_id.value = "bank-2";
            engine.banks.push_back(std::move(second_bank));
            auto second_head = profile.core.gas_path.heads.front();
            second_head.bank_id = BankId{2};
            profile.core.gas_path.heads.push_back(std::move(second_head));
            std::reverse(profile.core.gas_path.heads.begin(),
                         profile.core.gas_path.heads.end());
        },
        "gas_path.heads");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            harmonic_shape(only_cam_profile(profile.core.valvetrain.intake))
                .advance_rad.value = std::numeric_limits<double>::quiet_NaN();
        },
        "valvetrain.intake.profiles[0]");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            harmonic_shape(only_cam_profile(profile.core.valvetrain.intake))
                .construction_steps.value = 5;
        },
        "valvetrain.intake.profiles[0]");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            harmonic_shape(only_cam_profile(profile.core.valvetrain.intake))
                .maximum_lift_m.value = 0.0005;
        },
        "valvetrain.intake.profiles[0]");
    expect_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            harmonic_shape(only_cam_profile(profile.core.valvetrain.exhaust))
                .duration_at_reference_lift_rad.value =
                std::numeric_limits<double>::denorm_min();
        },
        "valvetrain.exhaust.profiles[0]");

    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            sampled_shape(only_cam_profile(profile.core.valvetrain.intake))
                .triangle_radius_rad.value = 0.0;
        },
        "valvetrain.intake.profiles[0]");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            sampled_shape(only_cam_profile(profile.core.valvetrain.exhaust))
                .base_radius_m.value = -0.001;
        },
        "valvetrain.exhaust.profiles[0]");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            sampled_shape(only_cam_profile(profile.core.valvetrain.intake))
                .advance_rad.value = std::numeric_limits<double>::infinity();
        },
        "valvetrain.intake.profiles[0]");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            sampled_shape(only_cam_profile(profile.core.valvetrain.intake))
                .samples.resize(1U);
        },
        "valvetrain.intake.profiles[0].shape.samples");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            auto &samples =
                sampled_shape(only_cam_profile(profile.core.valvetrain.intake)).samples;
            samples[1].angle_rad.value = samples[0].angle_rad.value;
        },
        "valvetrain.intake.profiles[0].shape.samples[1]");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            sampled_shape(only_cam_profile(profile.core.valvetrain.exhaust))
                .samples[1]
                .angle_rad.value = std::numeric_limits<double>::quiet_NaN();
        },
        "valvetrain.exhaust.profiles[0].shape.samples[1]");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            sampled_shape(only_cam_profile(profile.core.valvetrain.intake))
                .samples[1]
                .lift_m.value = -0.001;
        },
        "valvetrain.intake.profiles[0].shape.samples[1]");
    expect_sampled_compile_rejected(
        [](EngineSpec &, LowOrderOperatingPointV1Profile &profile) {
            auto &samples =
                sampled_shape(only_cam_profile(profile.core.valvetrain.exhaust))
                    .samples;
            samples[1].sample_id.value = samples[0].sample_id.value;
        },
        "valvetrain.exhaust.profiles[0].shape.samples");
}

void run_tests() {
    test_exact_lobe_construction_and_bindings();
    test_sampling_goldens_wrap_and_span_contract();
    test_sampled_lobe_copy_endpoints_wrap_advance_and_multiple_lobes();
    test_bank_local_cam_profiles_sample_by_cylinder_binding();
    test_bank_local_flow_profiles_bind_by_stable_bank_identity();
    test_focused_admission_failures();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "Legacy fixed valvetrain test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
