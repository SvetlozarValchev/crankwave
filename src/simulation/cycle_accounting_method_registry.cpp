#include "simulation/cycle_accounting_method_registry.hpp"

#include "simulation/chen_flynn_per_cylinder_travel_cycle_mean_loss.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace crankwave::simulation {
namespace {

static_assert(kFourStrokePiecewiseLinearCycleQuadratureMethodId !=
              kChenFlynnCycleMeanAggregateLossMethodId);
static_assert(kFourStrokePiecewiseLinearCycleQuadratureMethodId !=
              kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodId);
static_assert(kChenFlynnCycleMeanAggregateLossMethodId !=
              kChenFlynnPerCylinderPistonTravelCycleMeanAggregateLossMethodId);

[[nodiscard]] contract::Sha256Digest
descriptor_digest(std::string_view descriptor) noexcept {
    return contract::sha256(
        std::as_bytes(std::span<const char>{descriptor.data(), descriptor.size()}));
}

[[nodiscard]] contract::MethodIdentity
make_identity(std::string_view id, std::uint32_t version, std::string_view descriptor) {
    return {
        std::string{id},
        version,
        descriptor_digest(descriptor),
    };
}

void require_exact(contract::ValidationReport &report,
                   const contract::MethodIdentity &actual,
                   const contract::MethodIdentity &implemented, std::string path) {
    if (actual != implemented) {
        report.add(contract::ContractIssueCode::unsupported_value, std::move(path),
                   "method identity is not the exact implemented cycle-accounting "
                   "authority");
    }
}

} // namespace

const CycleAccountingMethodIdentities &
implemented_cycle_accounting_method_identities() {
    static const CycleAccountingMethodIdentities identities{
        make_identity(
            kFourStrokePiecewiseLinearCycleQuadratureMethodId,
            kFourStrokePiecewiseLinearCycleQuadratureMethodVersion,
            four_stroke_piecewise_linear_cycle_quadrature_method_descriptor()),
        make_identity(kChenFlynnCycleMeanAggregateLossMethodId,
                      kChenFlynnCycleMeanAggregateLossMethodVersion,
                      chen_flynn_cycle_mean_aggregate_loss_method_descriptor()),
        chen_flynn_per_cylinder_piston_travel_cycle_mean_aggregate_loss_method_identity(),
    };
    return identities;
}

const contract::MethodIdentity &implemented_aggregate_loss_method_identity_for(
    const contract::LowOrderOperatingPointV1Profile &profile) {
    const bool has_master_rod_kinematics =
        std::ranges::any_of(profile.core.mechanism.cylinders, [](const auto &cylinder) {
            return std::holds_alternative<contract::LegacyMasterRodJournalKinematics>(
                cylinder.kinematics);
        });
    const auto &implemented = implemented_cycle_accounting_method_identities();
    return has_master_rod_kinematics ? implemented.per_cylinder_travel_aggregate_loss
                                     : implemented.aggregate_loss;
}

const contract::MethodIdentity &
four_stroke_piecewise_linear_cycle_quadrature_method_identity() {
    return implemented_cycle_accounting_method_identities().cycle_quadrature;
}

const contract::MethodIdentity &chen_flynn_cycle_mean_aggregate_loss_method_identity() {
    return implemented_cycle_accounting_method_identities().aggregate_loss;
}

bool exactly_matches_implemented_cycle_accounting_methods(
    const contract::EngineSpec &engine,
    const contract::LowOrderOperatingPointV1Profile &profile) {
    const auto &implemented = implemented_cycle_accounting_method_identities();
    return profile.cycle_quadrature.value == implemented.cycle_quadrature &&
           engine.methods.losses.value ==
               implemented_aggregate_loss_method_identity_for(profile);
}

contract::ValidationReport admit_implemented_cycle_accounting_methods(
    const contract::EngineSpec &engine,
    const contract::LowOrderOperatingPointV1Profile &profile) {
    contract::ValidationReport report;
    const auto &implemented = implemented_cycle_accounting_method_identities();
    require_exact(report, profile.cycle_quadrature.value, implemented.cycle_quadrature,
                  "engine.physics_profile.cycle_quadrature.value");
    require_exact(report, engine.methods.losses.value,
                  implemented_aggregate_loss_method_identity_for(profile),
                  "engine.methods.losses.value");
    return report;
}

} // namespace crankwave::simulation
