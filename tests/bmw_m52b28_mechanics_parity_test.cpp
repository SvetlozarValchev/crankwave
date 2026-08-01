#include "authored_engine_fixture_support.hpp"
#include "reference/reference_parity_v1_reader.hpp"
#include "simulation/legacy_fixed_valvetrain.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"
#include "simulation/low_order_engine_core_v1_runtime_factory.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

constexpr double kLegacyPi = 3.14159265359;
constexpr double kLegacyRpmScale = 0.104719755;
constexpr double kMaximumCircularAngleErrorRad = 1.0e-12;
constexpr double kMaximumTimingErrorRad = 1.0e-14;

[[noreturn]] void fail(std::string message) {
    throw std::runtime_error{std::move(message)};
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(std::string{message});
    }
}

[[noreturn]] void fail_frame(std::uint64_t sample_index, std::string_view field) {
    std::ostringstream message;
    message << "frame " << sample_index << ": " << field;
    fail(message.str());
}

[[noreturn]] void fail_numeric(std::uint64_t sample_index, std::string_view field,
                               double actual, double expected) {
    std::ostringstream message;
    message << std::setprecision(17) << "frame " << sample_index << ": " << field
            << " (actual=" << actual << ", expected=" << expected << ')';
    fail(message.str());
}

[[nodiscard]] bool same_binary64(double lhs, double rhs) noexcept {
    return std::bit_cast<std::uint64_t>(lhs) == std::bit_cast<std::uint64_t>(rhs);
}

[[nodiscard]] double circular_error_4pi(double lhs, double rhs) noexcept {
    return std::abs(std::remainder(lhs - rhs, 4.0 * kLegacyPi));
}

[[nodiscard]] double expected_positive_mod(double value, double modulus) noexcept {
    if (value < 0.0) {
        value = std::ceil(-value / modulus) * modulus + value;
    }
    return std::fmod(value, modulus);
}

[[nodiscard]] std::vector<std::byte> read_exact_fixture(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    expect(input.is_open(), "could not open pinned reference-parity.bin");
    const auto size = input.tellg();
    expect(size == static_cast<std::streamoff>(reference::kReferenceParityV1ByteCount),
           "pinned reference-parity.bin has unexpected size");
    input.seekg(0);
    std::vector<std::byte> bytes(reference::kReferenceParityV1ByteCount);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
           "pinned reference-parity.bin read was incomplete");
    return bytes;
}

[[nodiscard]] reference::DecodedReferenceParityV1
decode_fixture(const std::vector<std::byte> &bytes) {
    auto result = reference::decode_reference_parity_v1(bytes);
    auto *decoded = std::get_if<reference::DecodedReferenceParityV1>(&result);
    expect(decoded != nullptr, "pinned reference-parity.bin failed strict decode");
    return std::move(*decoded);
}

[[nodiscard]] test::AuthoredEngineFixture
make_request(const test::AuthoredEngineFixture &canonical,
             const reference::DecodedReferenceParityV1 &fixture) {
    std::vector<double> rpm;
    rpm.reserve(fixture.frames.size());
    for (const auto &frame : fixture.frames) {
        rpm.push_back(frame.engine_speed_rpm);
    }

    auto request = test::make_prescribed_fixture(canonical, std::move(rpm));
    auto &scenario = request.scenario;
    scenario.scenario_id = "bmw-m52b28-reference-pull-v1";
    scenario.preparation = contract::FixedSettling{
        {1.0, "authored-fixture.parity.warm-up"},
        {1.0, "authored-fixture.parity.settling"},
    };
    scenario.operating_state = {
        {
            {
                "direction-acquisition",
                0.0,
                {false, true, true, false, true},
            },
            {
                "direction-lock",
                0.8,
                {false, true, true, true, true},
            },
            {
                "ignition-handoff",
                0.9,
                {true, true, false, true, true},
            },
        },
        "authored-fixture.parity.operating-state",
    };
    scenario.total_duration_s.value = 17.0;
    scenario.audible_start_s.value = 2.0;
    scenario.audible_duration_s.value = 15.0;
    scenario.quality.value = {
        "legacy-low-order-parity-v1",
        1,
        200,
        3800,
    };
    auto &sweep = std::get<contract::PrescribedKinematicSweep>(scenario.mode);
    sweep.throttle_01 = {
        contract::TrajectoryInterpolation::right_continuous_hold,
        {
            {0.0, 0.18},
            {0.9, 0.12},
            {1.0, 0.85},
        },
        "authored-fixture.parity.throttle",
    };
    return request;
}

struct CompiledMechanicsSession {
    simulation::LegacyLowOrderMechanicsSession session;
    simulation::SharedMechanismKinematicsPlan mechanism_plan;
    std::vector<simulation::CenteredSliderCrankCylinder> cylinder_models;
};

[[nodiscard]] CompiledMechanicsSession
compile_session(const test::AuthoredEngineFixture &request) {
    auto schedule_result =
        simulation::compile_kinematic_scenario_schedule(request.scenario);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&schedule_result)) {
        std::ostringstream message;
        message << "canonical BMW kinematic schedule failed to compile";
        for (const auto &issue : report->issues) {
            message << "\n  " << issue.path << ": " << issue.message;
        }
        fail(message.str());
    }
    const auto &schedule =
        std::get<simulation::KinematicScenarioSchedule>(schedule_result);
    const auto &core = test::low_order_core(request.engine);
    auto mechanism_plan_result =
        simulation::compile_mechanism_kinematics_plan(request.engine, core);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&mechanism_plan_result)) {
        std::ostringstream message;
        message << "canonical BMW mechanism plan failed to compile";
        for (const auto &issue : report->issues) {
            message << "\n  " << issue.path << ": " << issue.message;
        }
        fail(message.str());
    }
    auto mechanism_plan = std::get<simulation::SharedMechanismKinematicsPlan>(
        std::move(mechanism_plan_result));
    const auto *direct_plan =
        simulation::direct_mechanism_kinematics_plan(mechanism_plan);
    expect(direct_plan != nullptr,
           "canonical BMW did not compile a direct mechanism plan");
    std::vector<simulation::CenteredSliderCrankCylinder> cylinder_models;
    cylinder_models.reserve(direct_plan->cylinders.size());
    for (const auto &cylinder : direct_plan->cylinders) {
        cylinder_models.push_back(cylinder.crank);
    }
    auto result =
        simulation::detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
            request.engine, core, request.scenario, mechanism_plan, schedule);
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::ostringstream message;
        message << "canonical BMW mechanics request failed to compile";
        for (const auto &issue : report->issues) {
            message << "\n  " << issue.path << ": " << issue.message;
        }
        fail(message.str());
    }
    return {
        std::get<simulation::LegacyLowOrderMechanicsSession>(std::move(result)),
        std::move(mechanism_plan),
        std::move(cylinder_models),
    };
}

[[nodiscard]] simulation::LegacyFixedValvetrain
compile_valvetrain(const test::AuthoredEngineFixture &request) {
    auto result = simulation::compile_legacy_fixed_valvetrain(
        request.engine, test::low_order_core(request.engine));
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::ostringstream message;
        message << "canonical BMW valvetrain request failed to compile";
        for (const auto &issue : report->issues) {
            message << "\n  " << issue.path << ": " << issue.message;
        }
        fail(message.str());
    }
    return std::get<simulation::LegacyFixedValvetrain>(std::move(result));
}

void verify_shared_direct_plan_bits(
    const simulation::SharedMechanismKinematicsPlan &shared_plan,
    const contract::LowOrderEngineCoreV1 &core) {
    const auto *plan = simulation::direct_mechanism_kinematics_plan(shared_plan);
    expect(plan != nullptr, "BMW shared mechanism plan lost its direct alternative");
    const auto &mechanism = core.mechanism;
    expect(plan->output_crankshaft_id == mechanism.output_crankshaft_id &&
               same_binary64(plan->crank_tdc_reference_rad,
                             mechanism.cranks.front().crank_tdc_reference_rad.value) &&
               same_binary64(
                   plan->rigid_crank_group.authored_crank_inertia_kg_m2,
                   mechanism.cranks.front().authored_crank_inertia_kg_m2.value) &&
               plan->cylinders.size() == mechanism.cylinders.size(),
           "BMW shared mechanism crank or cylinder inventory changed");

    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &assembly = mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto &direct =
            std::get<contract::LegacyDirectJournalKinematics>(assembly.kinematics);
        const auto geometry = simulation::derive_legacy_cylinder_geometry(
            parameters.bore_m.value, direct.crank_radius_m.value,
            parameters.connecting_rod_length_m.value, parameters.deck_height_m.value,
            parameters.piston_compression_height_m.value,
            core.gas_path.heads.front().chamber_volume_m3.value,
            parameters.piston_displacement_term_m3.value);
        const double geometric_tdc_rad = simulation::legacy_wrap_2pi(
            mechanism.cranks.front().crank_tdc_reference_rad.value +
            direct.journal_angle_rad.value - simulation::kLegacyPi / 2.0);
        const auto &compiled = plan->cylinders[index];
        const auto &crank = compiled.crank;
        expect(compiled.crankshaft_id == assembly.topology.crankshaft_id &&
                   crank.cylinder_id == assembly.topology.cylinder_id &&
                   compiled.chamber_volume_id == assembly.topology.chamber_volume_id &&
                   compiled.exhaust_route_id == assembly.topology.exhaust_route_id &&
                   same_binary64(crank.geometric_tdc_rad, geometric_tdc_rad) &&
                   same_binary64(crank.piston_area_m2, geometry.piston_area_m2) &&
                   same_binary64(crank.crank_radius_m, direct.crank_radius_m.value) &&
                   same_binary64(crank.connecting_rod_length_m,
                                 parameters.connecting_rod_length_m.value) &&
                   same_binary64(crank.clearance_volume_m3,
                                 geometry.clearance_volume_m3) &&
                   same_binary64(crank.ignition_wire_angle_rad,
                                 parameters.ignition_wire_angle_rad.value) &&
                   same_binary64(compiled.bore_m, parameters.bore_m.value) &&
                   same_binary64(compiled.stroke_m, direct.stroke_m.value) &&
                   same_binary64(compiled.fixed_geometry_volume_m3,
                                 geometry.fixed_geometry_volume_m3) &&
                   same_binary64(compiled.piston_mass_kg,
                                 parameters.piston_mass_kg.value) &&
                   same_binary64(compiled.connecting_rod_mass_kg,
                                 parameters.connecting_rod_mass_kg.value) &&
                   same_binary64(compiled.connecting_rod_inertia_kg_m2,
                                 parameters.connecting_rod_inertia_kg_m2.value),
               "BMW shared mechanism cylinder fields changed bits or authored order");
    }

    const auto cycle_mean =
        simulation::calculate_centered_slider_crank_cycle_mean_inertia(mechanism);
    const auto *expected =
        std::get_if<simulation::CenteredSliderCrankCycleMeanInertia>(&cycle_mean);
    expect(expected != nullptr && plan->cycle_mean_inertia == *expected,
           "BMW shared mechanism plan changed the frozen cycle-mean inertia bits");
}

void verify_foreign_plan_is_rejected(const test::AuthoredEngineFixture &request) {
    auto foreign_engine = request.engine;
    foreign_engine.profile_id.value += "-foreign-plan";
    const auto &foreign_core = test::low_order_core(foreign_engine);
    auto foreign_plan_result =
        simulation::compile_mechanism_kinematics_plan(foreign_engine, foreign_core);
    expect(!std::holds_alternative<contract::ValidationReport>(foreign_plan_result),
           "foreign-plan fixture could not compile its own mechanism plan");
    auto schedule_result =
        simulation::compile_kinematic_scenario_schedule(request.scenario);
    expect(!std::holds_alternative<contract::ValidationReport>(schedule_result),
           "BMW foreign-plan check lost its kinematic schedule");
    auto result =
        simulation::detail::LowOrderEngineCoreV1RuntimeFactory::compile_mechanics(
            request.engine, test::low_order_core(request.engine), request.scenario,
            std::get<simulation::SharedMechanismKinematicsPlan>(
                std::move(foreign_plan_result)),
            std::get<simulation::KinematicScenarioSchedule>(
                std::move(schedule_result)));
    const auto *report = std::get_if<contract::ValidationReport>(&result);
    expect(report != nullptr &&
               std::any_of(
                   report->issues.begin(), report->issues.end(),
                   [](const auto &issue) { return issue.path == "mechanism_plan"; }),
           "mechanics admitted a mechanism plan compiled for another profile");
}

void verify_compiled_bmw_geometry(
    std::span<const simulation::CenteredSliderCrankCylinder> models) {
    constexpr std::array<double, 6> tdc_degrees{30.0, 150.0, 270.0, 270.0, 150.0, 30.0};
    constexpr std::array<double, 6> ignition_degrees{0.0,   480.0, 240.0,
                                                     600.0, 120.0, 360.0};
    constexpr double expected_area_m2 = 0.005541769440932761;
    constexpr double expected_clearance_m3 = 0.00004608105738123328;
    constexpr double expected_maximum_volume_m3 = 0.00051158969041958523;
    constexpr double derivative_dead_center_tolerance_m3_per_rad = 1.0e-14;
    constexpr double finite_difference_tolerance_m3_per_rad = 1.0e-12;
    constexpr double difference_step_rad = 1.0e-6;
    const double degree = kLegacyPi / 180.0;

    expect(models.size() == tdc_degrees.size(),
           "compiled BMW geometry has the wrong cylinder count");
    for (std::size_t index = 0; index < models.size(); ++index) {
        const auto &model = models[index];
        const double expected_tdc = tdc_degrees[index] * degree;
        const double expected_ignition = ignition_degrees[index] * degree;
        expect(model.cylinder_id ==
                   contract::CylinderId{static_cast<std::uint32_t>(index + 1U)},
               "compiled BMW cylinder identity/order changed");
        expect(same_binary64(model.piston_area_m2, expected_area_m2),
               "compiled BMW piston area changed");
        expect(same_binary64(model.clearance_volume_m3, expected_clearance_m3),
               "compiled BMW clearance volume changed");
        expect(std::abs(model.geometric_tdc_rad - expected_tdc) <= 2.0e-15,
               "compiled BMW geometric TDC changed");
        expect(std::abs(model.ignition_wire_angle_rad - expected_ignition) <= 2.0e-15,
               "compiled BMW ignition representative changed");
        expect(std::abs(model.geometric_tdc_rad -
                        expected_positive_mod(model.ignition_wire_angle_rad,
                                              2.0 * kLegacyPi) -
                        30.0 * degree) <= 3.0e-15,
               "mandatory BMW 30-degree geometry/ignition distinction changed");

        const auto tdc = simulation::evaluate_centered_slider_crank(
            model, model.geometric_tdc_rad, 1.0);
        const auto bdc = simulation::evaluate_centered_slider_crank(
            model, model.geometric_tdc_rad + kLegacyPi, 1.0);
        expect(tdc.valid && bdc.valid,
               "compiled BMW dead-center geometry became invalid");
        expect(std::abs(tdc.chamber_volume_m3 - expected_clearance_m3) <= 1.0e-18,
               "compiled BMW TDC volume changed");
        expect(std::abs(bdc.chamber_volume_m3 - expected_maximum_volume_m3) <= 1.0e-18,
               "compiled BMW BDC volume changed");
        expect(std::abs(tdc.dvolume_dtheta_m3_per_rad) <=
                       derivative_dead_center_tolerance_m3_per_rad &&
                   std::abs(bdc.dvolume_dtheta_m3_per_rad) <=
                       derivative_dead_center_tolerance_m3_per_rad,
               "compiled BMW dead-center volume derivative exceeded its gate");

        const double diagnostic_angle = model.geometric_tdc_rad + 0.73;
        const auto center =
            simulation::evaluate_centered_slider_crank(model, diagnostic_angle, 1.0);
        const auto before = simulation::evaluate_centered_slider_crank(
            model, diagnostic_angle - difference_step_rad, 1.0);
        const auto after = simulation::evaluate_centered_slider_crank(
            model, diagnostic_angle + difference_step_rad, 1.0);
        expect(center.valid && before.valid && after.valid,
               "compiled BMW finite-difference geometry became invalid");
        const double finite_difference =
            (after.chamber_volume_m3 - before.chamber_volume_m3) /
            (2.0 * difference_step_rad);
        expect(std::abs(center.dvolume_dtheta_m3_per_rad - finite_difference) <=
                   finite_difference_tolerance_m3_per_rad,
               "compiled BMW analytic dV/dtheta failed finite difference");
    }
}

void verify_compiled_bmw_valvetrain(const simulation::LegacyFixedValvetrain &valvetrain,
                                    const contract::LowOrderEngineCoreV1 &core) {
    constexpr std::array<double, 6> intake_stored_centers{
        0x1.067f5d701d783p+2, 0x1.094a40797a610p+3, 0x1.8c89ef31891d1p+2,
        0x1.2acce4e9d54a3p+3, 0x1.4984a650d34aap+2, 0x1.cf8f38123eef8p+2,
    };
    constexpr std::array<double, 6> exhaust_stored_centers{
        0x1.1cd675bb04be6p+1, 0x1.9a805e6059a8fp+2, 0x1.1475cc9eee041p+2,
        0x1.dd85a7410f7b6p+2, 0x1.a2e1077c70634p+1, 0x1.577b157fa3d68p+2,
    };

    const auto bindings = valvetrain.cylinder_bindings();
    expect(bindings.size() == core.mechanism.cylinders.size() &&
               bindings.size() == intake_stored_centers.size(),
           "compiled BMW valvetrain has the wrong cylinder count");
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const auto &binding = bindings[index];
        const auto &topology = core.mechanism.cylinders[index].topology;
        expect(binding.cylinder_id == topology.cylinder_id &&
                   binding.intake_port_id == topology.intake_port_id &&
                   binding.exhaust_port_id == topology.exhaust_port_id,
               "compiled BMW valve identity, order, or port binding changed");
        expect(same_binary64(binding.intake_stored_lobe_angle_rad,
                             intake_stored_centers[index]) &&
                   same_binary64(binding.exhaust_stored_lobe_angle_rad,
                                 exhaust_stored_centers[index]),
               "compiled BMW raw lobe center was wrapped or reconstructed");
    }
}

struct ValveCoverage {
    bool intake_zero = false;
    bool intake_nonzero = false;
    bool exhaust_zero = false;
    bool exhaust_nonzero = false;
    double maximum_intake_lift_m = 0.0;
    double maximum_exhaust_lift_m = 0.0;
};

void verify_valvetrain_step(const simulation::LegacyFixedValvetrain &valvetrain,
                            const simulation::LegacyMechanismStep &step,
                            std::array<ValveCoverage, 6> &coverage) {
    std::array<simulation::LegacyCylinderValveSample, 6> samples{};
    if (!valvetrain.sample_all(step.body_angle_psi_rad, samples)) {
        fail_frame(step.sample_index, "BMW valvetrain rejected a mechanics angle");
    }
    const auto bindings = valvetrain.cylinder_bindings();
    if (bindings.size() != samples.size()) {
        fail_frame(step.sample_index, "BMW valve sample count changed");
    }

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto &sample = samples[index];
        const auto &binding = bindings[index];
        if (sample.cylinder_id != binding.cylinder_id ||
            sample.intake_port_id != binding.intake_port_id ||
            sample.exhaust_port_id != binding.exhaust_port_id) {
            fail_frame(step.sample_index,
                       "BMW valve sample left its compiled cylinder binding");
        }
        const bool finite = std::isfinite(sample.intake_lobe_argument_rad) &&
                            std::isfinite(sample.exhaust_lobe_argument_rad) &&
                            std::isfinite(sample.intake_lift_m) &&
                            std::isfinite(sample.exhaust_lift_m) &&
                            std::isfinite(sample.intake_valve_k) &&
                            std::isfinite(sample.exhaust_valve_k);
        const bool bounded = sample.intake_lobe_argument_rad >= -kLegacyPi &&
                             sample.intake_lobe_argument_rad < kLegacyPi &&
                             sample.exhaust_lobe_argument_rad >= -kLegacyPi &&
                             sample.exhaust_lobe_argument_rad < kLegacyPi &&
                             sample.intake_lift_m >= 0.0 &&
                             sample.intake_lift_m <= 0.009000000000000001 &&
                             sample.exhaust_lift_m >= 0.0 &&
                             sample.exhaust_lift_m <= 0.009000000000000001 &&
                             sample.intake_valve_k >= 0.0 &&
                             sample.intake_valve_k <= 0.00632193692425645 &&
                             sample.exhaust_valve_k >= 0.0 &&
                             sample.exhaust_valve_k <= 0.004397869164700139;
        if (!finite || !bounded) {
            fail_frame(step.sample_index,
                       "BMW valve sample became nonfinite or left its profile bounds");
        }

        auto &observed = coverage[index];
        observed.intake_zero = observed.intake_zero || sample.intake_lift_m == 0.0;
        observed.intake_nonzero = observed.intake_nonzero || sample.intake_lift_m > 0.0;
        observed.exhaust_zero = observed.exhaust_zero || sample.exhaust_lift_m == 0.0;
        observed.exhaust_nonzero =
            observed.exhaust_nonzero || sample.exhaust_lift_m > 0.0;
        observed.maximum_intake_lift_m =
            std::max(observed.maximum_intake_lift_m, sample.intake_lift_m);
        observed.maximum_exhaust_lift_m =
            std::max(observed.maximum_exhaust_lift_m, sample.exhaust_lift_m);
    }
}

// Independent reconstruction of the sealed legacy triangle table. This deliberately
// does not call the simulator's sampler: the profile remains the source of both axes,
// and an upper point wins an equal-distance nearest-point tie.
[[nodiscard]] double
expected_timing_advance(const contract::LegacyIgnitionProfile &ignition,
                        double angular_speed_rad_s) {
    const auto &points = ignition.timing_curve;
    expect(!points.empty(), "sealed BMW ignition timing curve became empty");
    if (angular_speed_rad_s <= points.front().angular_speed_rad_s.value) {
        return points.front().timing_advance_rad.value;
    }
    if (angular_speed_rad_s >= points.back().angular_speed_rad_s.value) {
        return points.back().timing_advance_rad.value;
    }

    std::size_t closest = 0;
    double closest_distance =
        std::abs(points.front().angular_speed_rad_s.value - angular_speed_rad_s);
    for (std::size_t index = 1; index < points.size(); ++index) {
        const double distance =
            std::abs(points[index].angular_speed_rad_s.value - angular_speed_rad_s);
        if (distance <= closest_distance) {
            closest = index;
            closest_distance = distance;
        }
    }

    const double radius = ignition.timing_curve_triangle_radius_rad_s.value;
    double weighted_sum = 0.0;
    double total_weight = 0.0;
    for (std::size_t index = closest + 1U; index-- > 0U;) {
        const double x = points[index].angular_speed_rad_s.value;
        if (x > angular_speed_rad_s) {
            continue;
        }
        const double distance = std::abs(x - angular_speed_rad_s);
        if (distance > radius) {
            break;
        }
        const double weight = (radius - distance) / radius;
        weighted_sum += weight * points[index].timing_advance_rad.value;
        total_weight += weight;
    }
    for (std::size_t index = closest; index < points.size(); ++index) {
        const double x = points[index].angular_speed_rad_s.value;
        if (x <= angular_speed_rad_s) {
            continue;
        }
        const double distance = std::abs(x - angular_speed_rad_s);
        if (distance > radius) {
            break;
        }
        const double weight = (radius - distance) / radius;
        weighted_sum += weight * points[index].timing_advance_rad.value;
        total_weight += weight;
    }
    return total_weight != 0.0 ? weighted_sum / total_weight : 0.0;
}

void verify_controls(const simulation::LegacyMechanismStep &step,
                     const reference::ReferenceParityV1Frame &frame) {
    const auto sample_index = frame.sample_index;
    if (!same_binary64(step.requested_throttle_01,
                       frame.controls.requested_throttle_01)) {
        fail_numeric(sample_index, "requested throttle differs from fixture",
                     step.requested_throttle_01, frame.controls.requested_throttle_01);
    }
    if (!same_binary64(step.resolved_engine_throttle_01,
                       frame.controls.resolved_intake_throttle_01)) {
        fail_numeric(sample_index, "resolved throttle differs from fixture",
                     step.resolved_engine_throttle_01,
                     frame.controls.resolved_intake_throttle_01);
    }
    if (step.operating_state.ignition_enabled != frame.controls.ignition_enabled ||
        step.operating_state.fuel_enabled != frame.controls.fuel_enabled ||
        step.operating_state.starter_enabled != frame.controls.starter_enabled ||
        step.operating_state.dyno_enabled != frame.controls.dyno_enabled) {
        fail_frame(sample_index, "operating-state controls differ from fixture");
    }
    if (!step.operating_state.limiter_enabled) {
        fail_frame(sample_index, "sealed limiter configuration was not retained");
    }

    const double expected_plate = 0.994 * step.resolved_engine_throttle_01;
    const double expected_flow = std::cos(kLegacyPi * expected_plate / 2.0);
    if (!same_binary64(step.intake_plate_position_01, expected_plate) ||
        !same_binary64(step.main_flow_multiplier_01, expected_flow)) {
        fail_frame(sample_index, "direct intake throttle linkage changed");
    }
}

void verify_geometry(const simulation::LegacyMechanismStep &step,
                     const reference::DecodedReferenceParityV1 &fixture,
                     std::span<const simulation::CenteredSliderCrankCylinder> models) {
    if (step.cylinders.size() != reference::kReferenceParityV1CylinderCount ||
        step.cylinders.size() != models.size()) {
        fail_frame(step.sample_index, "mechanics cylinder count changed");
    }

    for (std::size_t index = 0; index < step.cylinders.size(); ++index) {
        const auto &cylinder = step.cylinders[index];
        const auto &descriptor = fixture.cylinders[index];
        const auto &model = models[index];
        if (cylinder.cylinder_id != contract::CylinderId{descriptor.stable_id} ||
            cylinder.exhaust_route_id !=
                contract::RouteId{descriptor.route_index + 1U} ||
            cylinder.cylinder_id != model.cylinder_id) {
            fail_frame(step.sample_index,
                       "mechanics cylinder identity, order, or route changed");
        }
        const auto *coordinates =
            std::get_if<simulation::DirectCylinderCoordinates>(&cylinder.coordinates);
        if (coordinates == nullptr) {
            fail_frame(step.sample_index,
                       "direct mechanics emitted non-direct coordinates");
        }
        const bool finite = std::isfinite(coordinates->geometric_tdc_rad) &&
                            std::isfinite(cylinder.ignition_wire_angle_rad) &&
                            std::isfinite(coordinates->phase_rad) &&
                            std::isfinite(coordinates->piston_travel_m) &&
                            std::isfinite(cylinder.chamber_volume_m3) &&
                            std::isfinite(coordinates->dx_dtheta_m_per_rad) &&
                            std::isfinite(cylinder.dvolume_dtheta_m3_per_rad) &&
                            std::isfinite(cylinder.piston_speed_abs_m_s);
        const bool physical =
            coordinates->phase_rad >= 0.0 && coordinates->phase_rad < 2.0 * kLegacyPi &&
            coordinates->piston_travel_m >= -1.0e-15 &&
            coordinates->piston_travel_m <= 2.0 * model.crank_radius_m + 1.0e-15 &&
            cylinder.chamber_volume_m3 > 0.0 && cylinder.piston_speed_abs_m_s >= 0.0;
        if (!finite || !physical) {
            fail_frame(step.sample_index,
                       "slider-crank geometry became nonfinite or nonphysical");
        }
    }
}

struct SparkSequenceState {
    std::optional<contract::CylinderId> previous_cylinder;
    std::array<bool, reference::kReferenceParityV1CylinderCount> seen{};
    std::uint64_t crossing_count = 0;
};

[[nodiscard]] contract::CylinderId
next_bmw_firing_cylinder(contract::CylinderId cylinder_id) {
    switch (cylinder_id.value) {
    case 1:
        return contract::CylinderId{5};
    case 5:
        return contract::CylinderId{3};
    case 3:
        return contract::CylinderId{6};
    case 6:
        return contract::CylinderId{2};
    case 2:
        return contract::CylinderId{4};
    case 4:
        return contract::CylinderId{1};
    default:
        fail("spark journal contained a non-BMW cylinder identity");
    }
}

void verify_spark_events(
    const simulation::LegacyMechanismStep &step,
    std::span<const simulation::CenteredSliderCrankCylinder> models,
    double previous_theta_cycle_rad, SparkSequenceState &sequence) {
    struct ExpectedCrossing {
        std::size_t runtime_index = 0;
        double adjusted_current_angle_rad = 0.0;
        double adjusted_spark_angle_rad = 0.0;
    };

    std::array<ExpectedCrossing, reference::kReferenceParityV1CylinderCount>
        expected_crossings{};
    std::size_t expected_count = 0;
    std::array<bool, reference::kReferenceParityV1CylinderCount> crossed{};
    if (step.operating_state.ignition_enabled) {
        for (std::size_t runtime_index = 0; runtime_index < models.size();
             ++runtime_index) {
            const auto &model = models[runtime_index];
            double adjusted_current = step.theta_cycle_rad;
            double adjusted_spark = expected_positive_mod(
                model.ignition_wire_angle_rad - step.timing_advance_rad,
                4.0 * kLegacyPi);
            bool expected_crossing = false;
            if (step.omega_legacy_rad_s < 0.0) {
                if (adjusted_current < previous_theta_cycle_rad) {
                    adjusted_current += 4.0 * kLegacyPi;
                    adjusted_spark += 4.0 * kLegacyPi;
                }
                expected_crossing = adjusted_spark >= previous_theta_cycle_rad &&
                                    adjusted_spark < adjusted_current;
            } else {
                if (adjusted_current > previous_theta_cycle_rad) {
                    adjusted_current -= 4.0 * kLegacyPi;
                    adjusted_spark -= 4.0 * kLegacyPi;
                }
                expected_crossing = adjusted_spark >= adjusted_current &&
                                    adjusted_spark < previous_theta_cycle_rad;
            }
            if (expected_crossing) {
                expected_crossings[expected_count++] = {runtime_index, adjusted_current,
                                                        adjusted_spark};
                crossed[runtime_index] = true;
            }
        }
    }

    expect(step.events.size() == expected_count,
           "BMW mechanics spark journal omitted or added a crossing");

    for (std::size_t event_index = 0; event_index < step.events.size(); ++event_index) {
        const auto &event = step.events[event_index];
        const auto &expected = expected_crossings[event_index];
        const auto &model = models[expected.runtime_index];
        expect(event.ordinal_within_step == event_index,
               "BMW mechanics event ordinals changed");
        const auto *spark = std::get_if<contract::SparkCrossing>(&event.payload);
        expect(spark != nullptr, "sub-limiter BMW mechanics emitted a non-spark event");
        expect(spark->cylinder_id == model.cylinder_id,
               "BMW spark journal left runtime cylinder order");
        expect(same_binary64(spark->raw_saved_angle_rad, previous_theta_cycle_rad) &&
                   same_binary64(spark->raw_current_angle_rad, step.theta_cycle_rad) &&
                   same_binary64(spark->adjusted_current_angle_rad,
                                 expected.adjusted_current_angle_rad) &&
                   same_binary64(spark->adjusted_spark_angle_rad,
                                 expected.adjusted_spark_angle_rad) &&
                   same_binary64(spark->timing_advance_rad, step.timing_advance_rad),
               "BMW spark crossing payload fields changed");

        if (sequence.previous_cylinder.has_value()) {
            expect(spark->cylinder_id ==
                       next_bmw_firing_cylinder(*sequence.previous_cylinder),
                   "BMW spark journal left firing order 1-5-3-6-2-4");
        }
        sequence.previous_cylinder = spark->cylinder_id;
        sequence.seen[expected.runtime_index] = true;
        ++sequence.crossing_count;
    }

    for (std::size_t index = 0; index < step.cylinders.size(); ++index) {
        expect(step.cylinders[index].spark_crossed == crossed[index],
               "BMW cylinder spark flag disagrees with its event journal");
    }
}

void verify_frame(const simulation::LegacyMechanismStep &step,
                  const reference::ReferenceParityV1Frame &frame,
                  const reference::DecodedReferenceParityV1 &fixture,
                  const contract::LegacyIgnitionProfile &ignition,
                  std::span<const simulation::CenteredSliderCrankCylinder> models,
                  double previous_theta_cycle_rad, SparkSequenceState &spark_sequence,
                  double &maximum_angle_error_rad) {
    if (step.rate != contract::RationalRateHz{10000, 1} ||
        step.sample_index != frame.sample_index ||
        step.step_end_index != frame.step_end ||
        step.timestamp_tick != frame.step_end) {
        fail_frame(frame.sample_index, "physics index, step end, or rate changed");
    }
    if (!same_binary64(step.engine_speed_rpm, frame.engine_speed_rpm)) {
        fail_numeric(frame.sample_index, "prescribed RPM differs from fixture",
                     step.engine_speed_rpm, frame.engine_speed_rpm);
    }
    if (!same_binary64(step.filtered_engine_speed_rpm,
                       frame.filtered_engine_speed_rpm)) {
        fail_numeric(frame.sample_index, "filtered RPM differs from fixture",
                     step.filtered_engine_speed_rpm, frame.filtered_engine_speed_rpm);
    }

    const double angle_error =
        circular_error_4pi(step.theta_cycle_rad, frame.crank_angle_rad);
    maximum_angle_error_rad = std::max(maximum_angle_error_rad, angle_error);
    if (!std::isfinite(angle_error) || angle_error > kMaximumCircularAngleErrorRad) {
        fail_numeric(frame.sample_index, "wrapped crank-angle parity failed",
                     step.theta_cycle_rad, frame.crank_angle_rad);
    }

    const double expected_omega_legacy = -frame.engine_speed_rpm * kLegacyRpmScale;
    if (!same_binary64(step.omega_legacy_rad_s, expected_omega_legacy) ||
        !same_binary64(step.angular_speed_rad_s, -expected_omega_legacy) ||
        !std::isfinite(step.angular_acceleration_rad_s2) ||
        !std::isfinite(step.body_angle_psi_rad) ||
        !std::isfinite(step.theta_unwrapped_rad)) {
        fail_frame(frame.sample_index, "prescribed crank-motion scalars changed");
    }

    const double expected_timing =
        expected_timing_advance(ignition, -expected_omega_legacy);
    if (!std::isfinite(step.timing_advance_rad) ||
        std::abs(step.timing_advance_rad - expected_timing) > kMaximumTimingErrorRad) {
        fail_numeric(frame.sample_index, "ignition timing lookup changed",
                     step.timing_advance_rad, expected_timing);
    }
    if (step.limiter_cut_active || step.limiter_timer_s != 0.0) {
        fail_frame(frame.sample_index,
                   "sub-limiter BMW trajectory unexpectedly activated the limiter");
    }

    verify_controls(step, frame);
    verify_geometry(step, fixture, models);
    verify_spark_events(step, models, previous_theta_cycle_rad, spark_sequence);
    if (step.events.size() > models.size() + 1U) {
        fail_frame(frame.sample_index, "mechanics event bound was exceeded");
    }
}

void test_full_bmw_mechanics_parity(const reference::DecodedReferenceParityV1 &fixture,
                                    const test::AuthoredEngineFixture &request) {
    expect(fixture.frames.size() == reference::kReferenceParityV1RecordCount,
           "frozen BMW fixture frame count changed");
    const auto &core = test::low_order_core(request.engine);
    auto compiled = compile_session(request);
    auto &session = compiled.session;
    const auto valvetrain = compile_valvetrain(request);
    const auto models = std::span<const simulation::CenteredSliderCrankCylinder>{
        compiled.cylinder_models};
    expect(models.size() == reference::kReferenceParityV1CylinderCount,
           "compiled BMW mechanics model count changed");
    verify_shared_direct_plan_bits(compiled.mechanism_plan, core);
    verify_foreign_plan_is_rejected(request);
    verify_compiled_bmw_geometry(models);
    verify_compiled_bmw_valvetrain(valvetrain, core);

    const auto &sweep =
        std::get<contract::PrescribedKinematicSweep>(request.scenario.mode);
    double previous_theta_cycle_rad = sweep.trajectory.initial_theta_rad.value;
    SparkSequenceState spark_sequence;
    std::array<ValveCoverage, reference::kReferenceParityV1CylinderCount>
        valve_coverage{};

    double maximum_angle_error_rad = 0.0;
    for (const auto &frame : fixture.frames) {
        auto result = session.advance();
        const auto *step =
            std::get_if<std::reference_wrapper<const simulation::LegacyMechanismStep>>(
                &result);
        if (step == nullptr) {
            if (const auto *fault = std::get_if<contract::FailureContext>(&result)) {
                std::ostringstream message;
                message << "frame " << frame.sample_index << ": mechanics fault "
                        << fault->detail_code << " (" << fault->state_summary << ')';
                fail(message.str());
            }
            fail_frame(frame.sample_index, "mechanics completed before fixture end");
        }
        verify_frame(step->get(), frame, fixture, core.ignition, models,
                     previous_theta_cycle_rad, spark_sequence, maximum_angle_error_rad);
        verify_valvetrain_step(valvetrain, step->get(), valve_coverage);
        previous_theta_cycle_rad = step->get().theta_cycle_rad;
    }

    expect(session.completed(),
           "mechanics session did not report completion after frame 169999");
    const auto first_terminal = session.advance();
    const auto *completed =
        std::get_if<simulation::LegacyMechanicsCompleted>(&first_terminal);
    expect(completed != nullptr &&
               completed->sample_count == reference::kReferenceParityV1RecordCount,
           "mechanics terminal result has the wrong produced sample count");
    const auto second_terminal = session.advance();
    const auto *second_completed =
        std::get_if<simulation::LegacyMechanicsCompleted>(&second_terminal);
    expect(second_completed != nullptr && completed != nullptr &&
               *second_completed == *completed,
           "mechanics completion result was not stable across repeated advance");
    expect(maximum_angle_error_rad <= kMaximumCircularAngleErrorRad,
           "maximum BMW crank-angle error exceeded the admitted parity bound");
    expect(spark_sequence.crossing_count > 0 &&
               std::all_of(spark_sequence.seen.begin(), spark_sequence.seen.end(),
                           [](bool seen) { return seen; }),
           "full BMW run did not exercise every cylinder's spark schedule");
    expect(std::all_of(valve_coverage.begin(), valve_coverage.end(),
                       [](const ValveCoverage &coverage) {
                           return coverage.intake_zero && coverage.intake_nonzero &&
                                  coverage.exhaust_zero && coverage.exhaust_nonzero &&
                                  coverage.maximum_intake_lift_m > 0.00899998 &&
                                  coverage.maximum_exhaust_lift_m > 0.00899998;
                       }),
           "full BMW run did not exercise every valve from closed through peak");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 3, "usage: bmw_m52b28_mechanics_parity_test <repository-root> "
                          "<reference-parity.bin>");
        const auto canonical = test::load_canonical_authored_engine_fixture(argv[1]);
        const auto bytes = read_exact_fixture(argv[2]);
        const auto fixture = decode_fixture(bytes);
        const auto request = make_request(canonical, fixture);
        test_full_bmw_mechanics_parity(fixture, request);
    } catch (const std::exception &error) {
        std::cerr << "BMW M52B28 mechanics parity test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
