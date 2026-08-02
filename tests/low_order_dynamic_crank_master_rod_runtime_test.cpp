#include "authored_engine_fixture_support.hpp"
#include "simulation/engine_sim_v1_transient_friction.hpp"
#include "simulation/legacy_gas_primitives.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/low_order_capture_plan.hpp"
#include "simulation/low_order_dynamic_crank_runtime.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <numbers>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation::detail {

struct LowOrderDynamicCrankRuntimeTestAccess {
    [[nodiscard]] static LowOrderDynamicCrankRuntime make_cold_free_engine(
        ScenarioControlCursor control_cursor,
        SharedMechanismKinematicsPlan mechanism_plan,
        LowOrderDynamicCrankMechanismRuntime mechanism_runtime,
        std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> piston_wall_cylinders,
        const contract::RationalRateHz rate,
        const LowOrderExecutionExtent execution_extent, const double initial_theta_rad,
        const double applied_positive_speed_crank_friction_torque_nm,
        const double starter_maximum_torque_nm, const double starter_target_speed_rad_s,
        std::string profile_id, std::string scenario_id,
        const contract::EngineId engine_id) {
        return LowOrderDynamicCrankRuntime{
            std::move(control_cursor),
            std::move(mechanism_plan),
            std::nullopt,
            std::nullopt,
            {},
            {},
            std::move(mechanism_runtime),
            std::move(piston_wall_cylinders),
            rate,
            execution_extent,
            0U,
            0.0,
            initial_theta_rad,
            true,
            applied_positive_speed_crank_friction_torque_nm,
            starter_maximum_torque_nm,
            starter_target_speed_rad_s,
            std::nullopt,
            std::nullopt,
            "low-order-master-rod-cold-internal-test",
            std::move(profile_id),
            std::move(scenario_id),
            engine_id,
        };
    }

    [[nodiscard]] static const LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime &
    radial_mechanism(const LowOrderDynamicCrankRuntime &runtime) {
        return std::get<LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>(
            runtime.mechanism_runtime_);
    }

    [[nodiscard]] static const std::vector<double> &
    retained_wall_reactions(const LowOrderDynamicCrankRuntime &runtime) {
        return runtime.retained_piston_wall_reaction_magnitude_n_;
    }

    [[nodiscard]] static double
    exact_crank_angular_speed_rad_s(const LowOrderDynamicCrankRuntime &runtime) {
        return runtime.crank_state_.angular_speed_rad_s;
    }
};

} // namespace engine_sim_offline::simulation::detail

namespace {

using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::simulation;

static_assert(!std::is_copy_constructible_v<
              LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>);
static_assert(std::is_nothrow_move_constructible_v<
              LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>);

void expect(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[noreturn]] void fail_report(const char *context, const ValidationReport &report) {
    std::ostringstream message;
    message << context;
    for (const auto &issue : report.issues) {
        message << "\n  " << issue.path << ": " << issue.message;
    }
    throw std::runtime_error{message.str()};
}

template <typename T>
[[nodiscard]] ResolvedValue<T> resolved(T value, std::string resolution_id) {
    return {std::move(value), std::move(resolution_id)};
}

[[nodiscard]] const OneLevelMasterRodCylinder &
radial_geometry(const OneLevelMasterRodMechanismCylinderPlan &cylinder) noexcept {
    return std::visit(
        [](const auto &kinematics) -> const OneLevelMasterRodCylinder & {
            return kinematics.cylinder;
        },
        cylinder.kinematics);
}

[[nodiscard]] std::size_t capture_volume_index(const LowOrderCapturePlan &plan,
                                               const GasVolumeId id) {
    const auto found =
        std::ranges::find(plan.capture_buffer.gas_volumes, id, &GasVolumeIdentity::id);
    expect(found != plan.capture_buffer.gas_volumes.end(),
           "radial chamber was absent from the capture gas inventory");
    return static_cast<std::size_t>(found - plan.capture_buffer.gas_volumes.begin());
}

[[nodiscard]] MethodIdentity internal_radial_crank_method() {
    MethodIdentity method;
    method.id = "internal-only-one-level-master-rod-cold-crank-test";
    method.version = 1U;
    method.configuration_sha256.bytes.front() = 1U;
    return method;
}

void test_cold_radial_dynamic_crank_executes_across_body_angle_wrap(
    const std::filesystem::path &repository_root) {
    constexpr double attached_inertia_kg_m2 = 0.11;
    constexpr double test_starter_maximum_torque_nm = 5000.0;
    constexpr double test_starter_target_speed_rad_s = 500.0;

    const auto fixture = engine_sim_offline::test::load_authored_engine_fixture(
        repository_root, "data/engines/radial-5-cleanroom/engine.json",
        "data/engines/radial-5-cleanroom/scenarios/prescribed-1500rpm.json");
    const auto frame_count = resolve_frame_index(
        fixture.scenario.total_duration_s.value, fixture.scenario.rates.physics);
    expect(frame_count.has_value() && *frame_count == 800U,
           "radial fixture lost its canonical 800-frame horizon");
    const auto extent = LowOrderExecutionExtent::finite_scenario(*frame_count);
    auto capture_compilation =
        compile_low_order_capture_plan(fixture.engine, fixture.scenario, extent);
    if (const auto *report = std::get_if<ValidationReport>(&capture_compilation)) {
        fail_report("prescribed radial capture plan failed compilation", *report);
    }
    const auto capture_plan =
        std::get<LowOrderCapturePlan>(std::move(capture_compilation));

    const auto &profile = engine_sim_offline::test::operating_profile(fixture.engine);
    auto mechanism_compilation =
        compile_mechanism_kinematics_plan(fixture.engine, profile.core);
    if (const auto *report = std::get_if<ValidationReport>(&mechanism_compilation)) {
        fail_report("radial mechanism plan failed compilation", *report);
    }
    auto mechanism_plan =
        std::get<SharedMechanismKinematicsPlan>(std::move(mechanism_compilation));
    const auto *radial_plan =
        one_level_master_rod_mechanism_kinematics_plan(mechanism_plan);
    expect(radial_plan != nullptr && radial_plan->cylinders.size() == 5U,
           "radial fixture did not compile its five-cylinder articulated plan");

    const double initial_body_angle_psi_rad = 0.0;
    const double initial_theta_rad = radial_plan->crank_tdc_reference_rad;
    // The public radial contract deliberately has no dynamic cycle-mean inertia.
    // These private FreeEngine fields are scheduler-only sentinels; the runtime below
    // consumes exact articulated M(psi) plus attached inertia instead.
    const double private_unconsumed_inertia_sentinel_kg_m2 =
        radial_plan->rigid_crank_group.authored_crank_inertia_kg_m2;

    auto scenario = fixture.scenario;
    scenario.scenario_id = "radial-5-internal-cold-dynamic-crank-wrap";
    scenario.mode = FreeEngine{
        resolved(0.0, "test.radial.initial-rpm"),
        resolved(initial_theta_rad, "test.radial.initial-theta"),
        resolved(private_unconsumed_inertia_sentinel_kg_m2,
                 "test.radial.engine-baseline-inertia"),
        resolved(attached_inertia_kg_m2, "test.radial.attached-inertia"),
        resolved(private_unconsumed_inertia_sentinel_kg_m2 + attached_inertia_kg_m2,
                 "test.radial.total-inertia"),
        {TrajectoryInterpolation::right_continuous_hold,
         {{0.0, 0.0}},
         "test.radial.throttle"},
        {TrajectoryInterpolation::right_continuous_hold,
         {{0.0, 0.0}},
         "test.radial.external-resistance"},
        resolved(internal_radial_crank_method(), "test.radial.crank-method"),
    };
    scenario.mode_resolution_id = "test.radial.internal-free-engine";
    scenario.preparation = FixedSettling{
        resolved(0.0, "test.radial.no-warm-up"),
        resolved(0.0, "test.radial.no-settling"),
    };
    scenario.operating_state.value = {
        {"cold-starter", 0.0, {false, false, true, false, false}},
    };
    scenario.audible_start_s.value = 0.0;
    scenario.audible_duration_s.value = scenario.total_duration_s.value;
    scenario.rates.capture = scenario.rates.physics;

    auto control_compilation = compile_scenario_control_schedule(scenario, extent);
    if (const auto *report = std::get_if<ValidationReport>(&control_compilation)) {
        fail_report("internal radial control schedule failed compilation", *report);
    }
    auto control_schedule =
        std::get<ScenarioControlSchedule>(std::move(control_compilation));

    auto articulated_compilation =
        compile_one_level_master_rod_articulated_mechanism(*radial_plan);
    const auto *articulated_error =
        std::get_if<OneLevelMasterRodConfigurationInertiaError>(
            &articulated_compilation);
    expect(articulated_error == nullptr,
           "radial fixture articulated mechanism failed compilation");
    auto articulated = std::get<CompiledOneLevelMasterRodArticulatedMechanism>(
        std::move(articulated_compilation));

    std::vector<LowOrderDynamicCrankPistonWallCylinderPlan> piston_wall_cylinders;
    std::vector<OneLevelMasterRodPistonWallBoundaryInput> boundaries;
    piston_wall_cylinders.reserve(radial_plan->cylinders.size());
    boundaries.reserve(radial_plan->cylinders.size());
    for (std::size_t index = 0; index < radial_plan->cylinders.size(); ++index) {
        const auto &planned = radial_plan->cylinders[index];
        const auto &geometry = radial_geometry(planned);
        const auto initial_mechanism = evaluate_one_level_master_rod_plan(
            *radial_plan, index, initial_body_angle_psi_rad, 0.0);
        expect(initial_mechanism.valid &&
                   std::isfinite(initial_mechanism.chamber_volume_m3) &&
                   initial_mechanism.chamber_volume_m3 > 0.0,
               "radial fixture rejected its cold initial mechanism state");
        const auto initial_cell = legacy_initialize_gas_cell(
            scenario.ambient.pressure_pa_abs.value, initial_mechanism.chamber_volume_m3,
            scenario.initial_thermal_state.gas_temperature_k.value,
            LegacyGasMixture{0.0, 1.0, 0.0});
        const double initial_pressure_pa_abs = legacy_gas_pressure_pa(initial_cell);
        expect(std::isfinite(initial_pressure_pa_abs) && initial_pressure_pa_abs > 0.0,
               "radial fixture produced an invalid initial chamber pressure");
        piston_wall_cylinders.push_back({
            geometry.cylinder_id,
            planned.chamber_volume_id,
            index,
            capture_volume_index(capture_plan, planned.chamber_volume_id),
            initial_pressure_pa_abs,
            scenario.crankcase.pressure_pa_abs.value,
            std::nullopt,
        });
        boundaries.push_back({
            geometry.cylinder_id,
            initial_pressure_pa_abs,
            scenario.crankcase.pressure_pa_abs.value,
            0.0,
        });
    }
    std::rotate(piston_wall_cylinders.begin(), piston_wall_cylinders.begin() + 2,
                piston_wall_cylinders.end());
    expect(piston_wall_cylinders.front().mechanism_cylinder_index == 2U,
           "test did not establish non-identity chamber-binding order");

    const auto crank_friction_calculation =
        calculate_engine_sim_v1_positive_speed_crank_friction(
            {radial_plan->rigid_crank_group.running_friction_torque_magnitude_nm});
    const auto *crank_friction =
        std::get_if<EngineSimV1PositiveSpeedCrankFriction>(&crank_friction_calculation);
    expect(crank_friction != nullptr,
           "radial fixture rejected its rigid crank friction");

    LowOrderDynamicCrankMechanismRuntime radial_runtime{
        LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime{
            std::move(articulated), attached_inertia_kg_m2, initial_body_angle_psi_rad,
            std::move(boundaries)}};
    const auto *const stable_articulated_owner =
        std::get<LowOrderDynamicCrankOneLevelMasterRodMechanismRuntime>(radial_runtime)
            .articulated_mechanism.get();
    auto dynamic = engine_sim_offline::simulation::detail::
        LowOrderDynamicCrankRuntimeTestAccess::make_cold_free_engine(
            control_schedule.fresh_cursor(), mechanism_plan, std::move(radial_runtime),
            std::move(piston_wall_cylinders), scenario.rates.physics, extent,
            initial_theta_rad, crank_friction->torque_nm,
            test_starter_maximum_torque_nm, test_starter_target_speed_rad_s,
            fixture.engine.profile_id.value, scenario.scenario_id, fixture.engine.id);
    expect(engine_sim_offline::simulation::detail::
                   LowOrderDynamicCrankRuntimeTestAccess::radial_mechanism(dynamic)
                       .articulated_mechanism.get() == stable_articulated_owner,
           "runtime construction changed the reaction workspace's mechanism owner");

    const auto random_plan = engine_sim_offline::test::compile_fixture_random_plan(
        fixture, fixture.engine, scenario);
    auto core_compilation = compile_low_order_engine_core_v1_runtime(
        fixture.engine, scenario, profile.core, random_plan, mechanism_plan, extent);
    if (const auto *report = std::get_if<ValidationReport>(&core_compilation)) {
        fail_report("internal radial core failed compilation", *report);
    }
    auto core = std::get<LowOrderEngineCoreV1Runtime>(std::move(core_compilation));

    double previous_body_angle_psi_rad = initial_body_angle_psi_rad;
    bool crossed_body_angle_wrap = false;
    bool observed_exact_omega_divergence = false;
    std::uint32_t continuation_ticks_after_wrap = 0U;
    for (std::uint64_t frame = 0U; frame < *frame_count; ++frame) {
        auto result = dynamic.advance(core);
        if (const auto *failure = std::get_if<FailureContext>(&result)) {
            throw std::runtime_error{
                "internal radial dynamic crank faulted: " + failure->detail_code +
                "; " + failure->state_summary};
        }
        const auto *step = std::get_if<LowOrderDynamicCrankStepView>(&result);
        expect(step != nullptr,
               "internal radial dynamic crank did not publish a physics step");
        const auto &mechanics = step->mechanics.get();
        const auto &gas = step->gas.get();
        expect(mechanics.sample_index == frame &&
                   mechanics.step_end_index == frame + 1U &&
                   gas.sample_index == mechanics.sample_index &&
                   gas.step_end_index == mechanics.step_end_index &&
                   mechanics.cylinders.size() == radial_plan->cylinders.size() &&
                   gas.cylinders.size() == mechanics.cylinders.size() &&
                   std::isfinite(mechanics.angular_speed_rad_s) &&
                   mechanics.angular_speed_rad_s > 0.0 &&
                   std::isfinite(gas.indicated_gas_torque_nm),
               "internal radial dynamic crank lost finite contiguous state");
        expect(core.produced_sample_count() == frame + 1U &&
                   dynamic.accepted_sample_count() == frame + 1U,
               "internal radial runtime and core lost count coherence");
        bool observed_physical_gas_volume = false;
        for (const auto &volume : gas.gas_volumes) {
            if (!volume.physically_resolved) {
                continue;
            }
            observed_physical_gas_volume = true;
            const double pressure_pa_abs = legacy_gas_pressure_pa(volume.cell);
            expect(std::isfinite(pressure_pa_abs) && pressure_pa_abs > 0.0,
                   "internal radial runtime published invalid gas pressure");
        }
        expect(observed_physical_gas_volume,
               "internal radial runtime published no physical gas volumes");
        for (const auto &cylinder : mechanics.cylinders) {
            const auto *coordinates =
                std::get_if<OneLevelMasterRodCoordinates>(&cylinder.coordinates);
            expect(coordinates != nullptr &&
                       std::isfinite(coordinates->piston_axis_position_m) &&
                       std::isfinite(coordinates->piston_axis_derivative_m_per_rad),
                   "internal radial dynamic crank lost articulated coordinates");
        }

        const auto &radial = engine_sim_offline::simulation::detail::
            LowOrderDynamicCrankRuntimeTestAccess::radial_mechanism(dynamic);
        expect(std::bit_cast<std::uint64_t>(radial.configuration_body_angle_psi_rad) ==
                       std::bit_cast<std::uint64_t>(mechanics.body_angle_psi_rad) &&
                   !radial.cached_configuration_inertia.has_value(),
               "internal radial dynamic crank did not retain exact body psi or "
               "clear its committed left-boundary M/M-prime transaction");
        const double exact_angular_speed_rad_s =
            engine_sim_offline::simulation::detail::
                LowOrderDynamicCrankRuntimeTestAccess::exact_crank_angular_speed_rad_s(
                    dynamic);
        expect(std::isfinite(exact_angular_speed_rad_s) &&
                   exact_angular_speed_rad_s > 0.0,
               "internal radial dynamic crank lost exact crank omega");
        observed_exact_omega_divergence =
            observed_exact_omega_divergence ||
            std::bit_cast<std::uint64_t>(exact_angular_speed_rad_s) !=
                std::bit_cast<std::uint64_t>(mechanics.angular_speed_rad_s);
        for (const double reaction : engine_sim_offline::simulation::detail::
                 LowOrderDynamicCrankRuntimeTestAccess::retained_wall_reactions(
                     dynamic)) {
            expect(std::isfinite(reaction) && reaction >= 0.0,
                   "internal radial dynamic crank retained an invalid wall reaction");
        }
        if (!crossed_body_angle_wrap && previous_body_angle_psi_rad < -12.0 &&
            mechanics.body_angle_psi_rad > -1.0) {
            crossed_body_angle_wrap = true;
            continuation_ticks_after_wrap = 2U;
        } else if (crossed_body_angle_wrap && continuation_ticks_after_wrap > 0U) {
            --continuation_ticks_after_wrap;
        }
        previous_body_angle_psi_rad = mechanics.body_angle_psi_rad;
        if (crossed_body_angle_wrap && continuation_ticks_after_wrap == 0U) {
            break;
        }
    }
    if (!crossed_body_angle_wrap || continuation_ticks_after_wrap != 0U ||
        !observed_exact_omega_divergence || dynamic.accepted_sample_count() <= 2U ||
        dynamic.faulted() || dynamic.finalized() ||
        !(previous_body_angle_psi_rad < 0.0)) {
        std::ostringstream message;
        message << "cold radial dynamic crank did not cross the body-angle wrap and "
                   "continue with exact omega; crossed="
                << crossed_body_angle_wrap
                << "; continuation=" << continuation_ticks_after_wrap
                << "; exact-omega-diverged=" << observed_exact_omega_divergence
                << "; accepted=" << dynamic.accepted_sample_count()
                << "; finalized=" << dynamic.finalized()
                << "; psi=" << previous_body_angle_psi_rad;
        throw std::runtime_error{message.str()};
    }
}

} // namespace

int main(const int argc, char **argv) {
    expect(argc == 2, "expected repository root argument");
    test_cold_radial_dynamic_crank_executes_across_body_angle_wrap(argv[1]);
    return 0;
}
