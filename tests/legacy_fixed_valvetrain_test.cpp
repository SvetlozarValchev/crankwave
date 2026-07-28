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

[[nodiscard]] LegacyLowOrderV1Profile &legacy_profile(EngineSpec &engine) {
    auto *profile = std::get_if<LegacyLowOrderV1Profile>(&engine.physics_profile);
    expect(profile != nullptr, "test engine lost its legacy profile");
    return *profile;
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
        auto &profile = legacy_profile(engine);
        auto configure_shape = [](LegacyCamShape &shape) {
            const double centimetre_source = 1.0 / 100.0;
            const double inch_source = centimetre_source * 2.54;
            shape.maximum_lift_m.value = kMaximumLiftM;
            shape.duration_at_reference_lift_rad.value = 210.0 * kDegreeSource;
            shape.exponent.value = 0.8;
            shape.construction_steps.value = 100;
            shape.advance_rad.value = 0.0;
            shape.base_radius_m.value = 0.6 * inch_source;
        };
        configure_shape(profile.valvetrain.intake.shape);
        configure_shape(profile.valvetrain.exhaust.shape);

        constexpr std::array<double, 13> intake_cfm{
            0.0,   35.0,  60.0,  90.0,  125.0, 150.0, 175.0,
            200.0, 215.0, 230.0, 235.0, 235.0, 238.0,
        };
        constexpr std::array<double, 13> exhaust_cfm{
            0.0,   35.0,  55.0,  85.0,  105.0, 120.0, 140.0,
            150.0, 155.0, 160.0, 165.0, 165.0, 165.0,
        };
        profile.gas_path.head.intake_flow =
            make_flow_table(profile.gas_path.head.intake_flow.front(), intake_cfm);
        profile.gas_path.head.exhaust_flow =
            make_flow_table(profile.gas_path.head.exhaust_flow.front(), exhaust_cfm);
        profile.gas_path.head.flow_table_triangle_radius_m.value = 0.001;

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

        auto second_mechanism = profile.mechanism.cylinders.front();
        second_mechanism.topology.cylinder_id = CylinderId{2};
        second_mechanism.topology.intake_port_id = PortId{3};
        second_mechanism.topology.exhaust_port_id = PortId{4};
        profile.mechanism.cylinders.push_back(std::move(second_mechanism));

        profile.valvetrain.intake.lobes.front().crank_center_rad.value = 0.0;
        profile.valvetrain.exhaust.lobes.front().crank_center_rad.value = 0.0;
        auto second_intake_lobe = profile.valvetrain.intake.lobes.front();
        second_intake_lobe.cylinder_id = CylinderId{2};
        second_intake_lobe.port_id = PortId{3};
        second_intake_lobe.crank_center_rad.value = 710.0 * kDegreeSource;
        auto second_exhaust_lobe = profile.valvetrain.exhaust.lobes.front();
        second_exhaust_lobe.cylinder_id = CylinderId{2};
        second_exhaust_lobe.port_id = PortId{4};
        second_exhaust_lobe.crank_center_rad.value = 855.0 * kDegreeSource;
        profile.valvetrain.intake.lobes.push_back(std::move(second_intake_lobe));
        profile.valvetrain.exhaust.lobes.push_back(std::move(second_exhaust_lobe));

        // Admission must resolve by stable cylinder identity, not lobe-vector order.
        std::reverse(profile.valvetrain.intake.lobes.begin(),
                     profile.valvetrain.intake.lobes.end());
        std::reverse(profile.valvetrain.exhaust.lobes.begin(),
                     profile.valvetrain.exhaust.lobes.end());
    }
};

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
    mutation(fixture.engine, legacy_profile(fixture.engine));
    auto result = compile_legacy_fixed_valvetrain(fixture.engine);
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

void test_exact_lobe_construction_and_bindings() {
    ValvetrainFixture fixture;
    const auto &profile = legacy_profile(fixture.engine);
    auto valvetrain =
        require_valvetrain(compile_legacy_fixed_valvetrain(fixture.engine));

    const auto intake = valvetrain.intake_lobe_table();
    const auto exhaust = valvetrain.exhaust_lobe_table();
    expect(intake.size() == 199 && exhaust.size() == 199,
           "N=100 did not produce two 199-sample lobe tables");
    expect(same_binary64(valvetrain.intake_lobe_triangle_radius_rad(),
                         kExpectedLobeRadiusRad) &&
               same_binary64(valvetrain.exhaust_lobe_triangle_radius_rad(),
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
               bindings[1].cylinder_id == CylinderId{2} &&
               bindings[1].intake_port_id == PortId{3} &&
               bindings[1].exhaust_port_id == PortId{4},
           "lobe-vector reordering changed admitted cylinder/port order");
    const auto intake_lobe_2 = std::find_if(
        profile.valvetrain.intake.lobes.begin(), profile.valvetrain.intake.lobes.end(),
        [](const auto &lobe) { return lobe.cylinder_id == CylinderId{2}; });
    const auto exhaust_lobe_2 =
        std::find_if(profile.valvetrain.exhaust.lobes.begin(),
                     profile.valvetrain.exhaust.lobes.end(), [](const auto &lobe) {
                         return lobe.cylinder_id == CylinderId{2};
                     });
    expect(intake_lobe_2 != profile.valvetrain.intake.lobes.end() &&
               exhaust_lobe_2 != profile.valvetrain.exhaust.lobes.end() &&
               same_binary64(bindings[1].intake_stored_lobe_angle_rad,
                             intake_lobe_2->crank_center_rad.value / 2.0) &&
               same_binary64(bindings[1].exhaust_stored_lobe_angle_rad,
                             exhaust_lobe_2->crank_center_rad.value / 2.0),
           "raw crank centers were wrapped or reordered before half-speed storage");
}

void test_sampling_goldens_wrap_and_span_contract() {
    ValvetrainFixture fixture;
    auto valvetrain =
        require_valvetrain(compile_legacy_fixed_valvetrain(fixture.engine));

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
    expect(peak->intake_lift_m < valvetrain.intake_base_radius_m() &&
               peak->exhaust_lift_m < valvetrain.exhaust_base_radius_m(),
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

void test_focused_admission_failures() {
    expect_compile_rejected(
        [](EngineSpec &engine, LegacyLowOrderV1Profile &) {
            engine.methods.valvetrain.value.id = "unsupported-valvetrain";
        },
        "engine.methods.valvetrain");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.valvetrain.intake.lobes[0].cylinder_id =
                profile.valvetrain.intake.lobes[1].cylinder_id;
        },
        "valvetrain.intake.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.valvetrain.exhaust.lobes.pop_back();
        },
        "valvetrain.exhaust.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            auto &cylinder_one = *std::find_if(
                profile.valvetrain.intake.lobes.begin(),
                profile.valvetrain.intake.lobes.end(),
                [](const auto &lobe) { return lobe.cylinder_id == CylinderId{1}; });
            cylinder_one.port_id = PortId{2};
        },
        "valvetrain.intake.lobes");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.gas_path.head.intake_flow[1].lift_m.value =
                profile.gas_path.head.intake_flow[0].lift_m.value;
        },
        "gas_path.head.intake_flow");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.gas_path.head.exhaust_flow.resize(1);
        },
        "gas_path.head.exhaust_flow");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.valvetrain.intake.shape.advance_rad.value =
                std::numeric_limits<double>::quiet_NaN();
        },
        "valvetrain.intake.shape");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.valvetrain.intake.shape.construction_steps.value = 5;
        },
        "valvetrain.intake.shape");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.valvetrain.intake.shape.maximum_lift_m.value = 0.0005;
        },
        "valvetrain.intake.shape");
    expect_compile_rejected(
        [](EngineSpec &, LegacyLowOrderV1Profile &profile) {
            profile.valvetrain.exhaust.shape.duration_at_reference_lift_rad.value =
                std::numeric_limits<double>::denorm_min();
        },
        "valvetrain.exhaust.shape");
}

void run_tests() {
    test_exact_lobe_construction_and_bindings();
    test_sampling_goldens_wrap_and_span_contract();
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
