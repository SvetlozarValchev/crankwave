#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

} // namespace

MechanismKinematicsPlanCompileResult
compile_mechanism_kinematics_plan(const contract::EngineSpec &engine,
                                  const contract::LowOrderEngineCoreV1 &core) {
    ValidationReport report;
    const auto &mechanism = core.mechanism;
    const auto &crank = mechanism.crank;

    require(report, std::isfinite(crank.crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank.crank_tdc_reference_rad.value",
            "crank TDC reference must be finite");
    require(report,
            mechanism.cylinders.size() == engine.cylinders.size() &&
                !mechanism.cylinders.empty(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "mechanism cylinders must match the nonempty engine cylinder order");
    require(report,
            mechanism.cylinders.size() <= std::numeric_limits<std::uint8_t>::max(),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.mechanism.cylinders",
            "event ordinals support at most 255 cylinders per mechanics session");

    DirectMechanismKinematicsPlan plan;
    plan.engine_id = engine.id;
    plan.engine_profile_id = engine.profile_id.value;
    plan.crank_tdc_reference_rad = crank.crank_tdc_reference_rad.value;
    plan.authored_crank_inertia_kg_m2 =
        crank.authored_crank_inertia_kg_m2.value;
    plan.cylinders.reserve(mechanism.cylinders.size());

    std::unordered_set<std::uint32_t> cylinder_ids;
    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &assembly = mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto path =
            "engine.physics_profile.mechanism.cylinders[" +
            std::to_string(index) + "]";
        const bool identity_valid =
            assembly.topology.cylinder_id.valid() && index < engine.cylinders.size() &&
            assembly.topology.cylinder_id == engine.cylinders[index].id &&
            cylinder_ids.insert(assembly.topology.cylinder_id.value).second;
        require(report, identity_valid, ContractIssueCode::inconsistent_semantics,
                path + ".topology.cylinder_id",
                "mechanism cylinder identity/order must be unique and match the "
                "engine topology");
        const auto route = std::find_if(
            engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                return candidate.id == assembly.topology.exhaust_route_id;
            });
        const bool exhaust_route_valid =
            assembly.topology.exhaust_route_id.valid() &&
            route != engine.routes.end() &&
            route->kind.value == contract::SourceRouteKind::exhaust_outlet;
        require(report, exhaust_route_valid, ContractIssueCode::dangling_reference,
                path + ".topology.exhaust_route_id",
                "mechanism cylinder requires an existing exhaust-outlet route");

        const double bore_m = parameters.bore_m.value;
        const double crank_radius_m = parameters.crank_radius_m.value;
        const double rod_length_m = parameters.connecting_rod_length_m.value;
        const double deck_height_m = parameters.deck_height_m.value;
        const double compression_height_m =
            parameters.piston_compression_height_m.value;
        const double head_volume_m3 = parameters.head_chamber_volume_m3.value;
        const double piston_displacement_m3 =
            parameters.piston_displacement_term_m3.value;
        const double journal_angle_rad = parameters.journal_angle_rad.value;
        const double ignition_wire_angle_rad =
            parameters.ignition_wire_angle_rad.value;

        const bool numeric_inputs_valid =
            finite_positive(bore_m) && finite_positive(crank_radius_m) &&
            finite_positive(rod_length_m) && crank_radius_m < rod_length_m &&
            finite_positive(deck_height_m) && finite_positive(compression_height_m) &&
            finite_positive(head_volume_m3) &&
            std::isfinite(piston_displacement_m3) &&
            std::isfinite(journal_angle_rad) &&
            std::isfinite(ignition_wire_angle_rad);
        require(report, numeric_inputs_valid, ContractIssueCode::invalid_value,
                path + ".parameters",
                "centered slider-crank inputs must be finite, positive where "
                "required, and have crank radius below rod length");
        if (!numeric_inputs_valid || !identity_valid || !exhaust_route_valid ||
            !std::isfinite(crank.crank_tdc_reference_rad.value)) {
            continue;
        }

        // Keep this direct-path calculation sequence byte-for-byte aligned with
        // the former mechanics compiler: derive first, then form/wrap TDC.
        const auto geometry = derive_legacy_cylinder_geometry(
            bore_m, crank_radius_m, rod_length_m, deck_height_m,
            compression_height_m, head_volume_m3, piston_displacement_m3);
        const double geometric_tdc_rad = legacy_wrap_2pi(
            crank.crank_tdc_reference_rad.value + journal_angle_rad -
            kLegacyPi / 2.0);
        const bool derived_valid = finite_positive(geometry.piston_area_m2) &&
                                   finite_positive(geometry.clearance_volume_m3) &&
                                   finite_positive(geometry.fixed_geometry_volume_m3) &&
                                   std::isfinite(geometric_tdc_rad);
        require(report, derived_valid, ContractIssueCode::invalid_value, path,
                "compiled slider-crank area, clearance, and phase must be valid");
        if (!derived_valid) {
            continue;
        }

        plan.cylinders.push_back({
            {
                assembly.topology.cylinder_id,
                geometric_tdc_rad,
                geometry.piston_area_m2,
                crank_radius_m,
                rod_length_m,
                geometry.clearance_volume_m3,
                ignition_wire_angle_rad,
            },
            assembly.topology.chamber_volume_id,
            assembly.topology.exhaust_route_id,
            bore_m,
            deck_height_m,
            compression_height_m,
            head_volume_m3,
            piston_displacement_m3,
            journal_angle_rad,
            geometry.fixed_geometry_volume_m3,
            parameters.piston_mass_kg.value,
            parameters.connecting_rod_mass_kg.value,
            parameters.connecting_rod_inertia_kg_m2.value,
        });
    }

    const auto cycle_mean_calculation =
        calculate_centered_slider_crank_cycle_mean_inertia(mechanism);
    const auto *cycle_mean =
        std::get_if<CenteredSliderCrankCycleMeanInertia>(&cycle_mean_calculation);
    require(report, cycle_mean != nullptr, ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism",
            "configuration-dependent inertia rejected the admitted centered-slider "
            "mechanism");
    require(report, plan.cylinders.size() == mechanism.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "direct mechanism plan must cover every mechanism cylinder exactly "
            "once");
    if (!report.ok() || cycle_mean == nullptr) {
        return report;
    }
    plan.cycle_mean_inertia = *cycle_mean;

    return std::make_shared<const MechanismKinematicsPlan>(
        std::in_place_type<DirectMechanismKinematicsPlan>, std::move(plan));
}

const DirectMechanismKinematicsPlan *direct_mechanism_kinematics_plan(
    const SharedMechanismKinematicsPlan &plan) noexcept {
    return plan == nullptr
               ? nullptr
               : std::get_if<DirectMechanismKinematicsPlan>(plan.get());
}

bool mechanism_kinematics_plan_matches_source(
    const SharedMechanismKinematicsPlan &plan,
    const contract::EngineSpec &engine,
    const contract::LowOrderEngineCoreV1 &core) noexcept {
    const auto *direct = direct_mechanism_kinematics_plan(plan);
    const auto &mechanism = core.mechanism;
    if (direct == nullptr || direct->engine_id != engine.id ||
        direct->engine_profile_id != engine.profile_id.value ||
        direct->cylinders.empty() ||
        direct->cylinders.size() != engine.cylinders.size() ||
        direct->cylinders.size() != mechanism.cylinders.size() ||
        !same_binary64(direct->crank_tdc_reference_rad,
                       mechanism.crank.crank_tdc_reference_rad.value) ||
        !same_binary64(direct->authored_crank_inertia_kg_m2,
                       mechanism.crank.authored_crank_inertia_kg_m2.value) ||
        !same_binary64(direct->cycle_mean_inertia.authored_crank_inertia_kg_m2,
                       mechanism.crank.authored_crank_inertia_kg_m2.value)) {
        return false;
    }

    for (std::size_t index = 0; index < direct->cylinders.size(); ++index) {
        const auto &planned = direct->cylinders[index];
        const auto &assembly = mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto route = std::find_if(
            engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                return candidate.id == planned.exhaust_route_id;
            });
        if (planned.crank.cylinder_id != engine.cylinders[index].id ||
            planned.crank.cylinder_id != assembly.topology.cylinder_id ||
            planned.chamber_volume_id != assembly.topology.chamber_volume_id ||
            planned.exhaust_route_id != assembly.topology.exhaust_route_id ||
            route == engine.routes.end() ||
            route->kind.value != contract::SourceRouteKind::exhaust_outlet ||
            !same_binary64(planned.bore_m, parameters.bore_m.value) ||
            !same_binary64(planned.crank.crank_radius_m,
                           parameters.crank_radius_m.value) ||
            !same_binary64(planned.crank.connecting_rod_length_m,
                           parameters.connecting_rod_length_m.value) ||
            !same_binary64(planned.deck_height_m,
                           parameters.deck_height_m.value) ||
            !same_binary64(planned.piston_compression_height_m,
                           parameters.piston_compression_height_m.value) ||
            !same_binary64(planned.head_chamber_volume_m3,
                           parameters.head_chamber_volume_m3.value) ||
            !same_binary64(planned.piston_displacement_term_m3,
                           parameters.piston_displacement_term_m3.value) ||
            !same_binary64(planned.authored_journal_angle_rad,
                           parameters.journal_angle_rad.value) ||
            !same_binary64(planned.crank.ignition_wire_angle_rad,
                           parameters.ignition_wire_angle_rad.value) ||
            !same_binary64(planned.piston_mass_kg,
                           parameters.piston_mass_kg.value) ||
            !same_binary64(planned.connecting_rod_mass_kg,
                           parameters.connecting_rod_mass_kg.value) ||
            !same_binary64(planned.connecting_rod_inertia_kg_m2,
                           parameters.connecting_rod_inertia_kg_m2.value)) {
            return false;
        }
    }
    return true;
}

} // namespace engine_sim_offline::simulation
